/*
 * RISC-V translation routines for the RVXI Base Integer Instruction Set.
 *
 * Copyright (c) 2020 Western Digital
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef CONFIG_USER_ONLY
#define do_hlv(ctx, a, mop, func)  false
#define do_hsv(ctx, a, mop, func)  false
#define do_hlvx(ctx, a, func)      false
#else
static void gen_helper_hyp_hlv_b(TCGv r, TCGv_env e, TCGv a)
{
    gen_helper_hyp_hlv_bu(r, e, a);
    tcg_gen_ext8s_tl(r, r);
}

static void gen_helper_hyp_hlv_h(TCGv r, TCGv_env e, TCGv a)
{
    gen_helper_hyp_hlv_hu(r, e, a);
    tcg_gen_ext16s_tl(r, r);
}

static void gen_helper_hyp_hlv_w(TCGv r, TCGv_env e, TCGv a)
{
    gen_helper_hyp_hlv_wu(r, e, a);
    tcg_gen_ext32s_tl(r, r);
}

/*
 * do_hlv()/do_hsv() cover the HLV/HSV instructions proper (the ones that
 * take a plain GPR address operand). The effective privilege for the access
 * depends on hstatus.SPVP/vsstatus.SUM (check_access_hlsv(),
 * target/riscv/op_helper.c), which is only known at runtime, so mmu_idx is
 * computed inside a runtime helper rather than baked into a translate-time
 * constant.
 *
 * HLV/HSV also have a CHERI "capmode" operand: when ctx->capmode is set,
 * rs1 is a capability register and the address must be bounds/permission
 * checked against it (generate_cap_load_check_imm()/
 * generate_cap_store_check_imm(), cheri-translate-utils.h) rather than
 * against DDC. That check still runs at translate time, since it only
 * depends on the capability register's static bounds, not on any
 * runtime-only privilege state; the resulting checked address is then
 * passed to helper_hyp_load_checked()/helper_hyp_store_checked()
 * (op_helper.c), which resolve mmu_idx via check_access_hlsv() and perform
 * the actual memory access, mirroring how the non-capmode/DDC case's bounds
 * check (check_ddc(), op_helper.c) and memory access both happen inside the
 * helper_hyp_h[ls]v_*() helpers below.
 */
static bool do_hlv(DisasContext *ctx, arg_r2 *a, MemOp mop,
                   void (*func)(TCGv, TCGv_env, TCGv))
{
    decode_save_opc(ctx);
#ifdef TARGET_CHERI
    if (ctx->capmode) {
        gen_helper_hyp_check_access(cpu_env);
        TCGv_cap_checked_ptr vaddr = tcg_temp_new_cap_checked();
        generate_cap_load_check_imm(vaddr, a->rs1, 0, mop);
        TCGv dest = dest_gpr(ctx, a->rd);
        gen_helper_hyp_load_checked(dest, cpu_env, (TCGv)vaddr,
                                    tcg_constant_i32(mop));
        gen_set_gpr(ctx, a->rd, dest);
        return true;
    }
#endif
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
    func(dest, cpu_env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool do_hsv(DisasContext *ctx, arg_r2_s *a, MemOp mop,
                   void (*func)(TCGv_env, TCGv, TCGv))
{
    decode_save_opc(ctx);
#ifdef TARGET_CHERI
    if (ctx->capmode) {
        gen_helper_hyp_check_access(cpu_env);
        TCGv_cap_checked_ptr vaddr = tcg_temp_new_cap_checked();
        generate_cap_store_check_imm(vaddr, a->rs1, 0, mop);
        TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
        gen_helper_hyp_store_checked(cpu_env, (TCGv)vaddr, data,
                                     tcg_constant_i32(mop));
        return true;
    }
#endif
    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
    func(cpu_env, addr, data);
    return true;
}

/*
 * HLVX (used by the VS-stage software instruction-fetch page-fault
 * handler to read guest instruction memory with execute permission) is
 * kept as a separate, simpler function rather than folded into do_hlv()
 * above: even before this commit, HLVX on this fork never went through
 * the CHERI capmode/DDC-interposed path (its pre-image do_hlvx() below
 * had no ctx->capmode branch and never called gen_ddc_interposed_ld_tl()
 * -- unlike do_hlv()/do_hsv()). Reusing do_hlv() for it, as upstream now
 * does, would newly send it through the capmode branch and change its
 * behaviour; this preserves the fork's pre-existing (CHERI-unaware) HLVX
 * semantics unchanged.
 */
static bool do_hlvx(DisasContext *ctx, arg_r2 *a,
                    void (*func)(TCGv, TCGv_env, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);

    decode_save_opc(ctx);
    func(dest, cpu_env, addr);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
#endif /* CONFIG_USER_ONLY */

static bool trans_hlv_b(DisasContext *ctx, arg_hlv_b *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_SB, gen_helper_hyp_hlv_b);
}

static bool trans_hlv_h(DisasContext *ctx, arg_hlv_h *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TESW, gen_helper_hyp_hlv_h);
}

static bool trans_hlv_w(DisasContext *ctx, arg_hlv_w *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TESL, gen_helper_hyp_hlv_w);
}

static bool trans_hlv_bu(DisasContext *ctx, arg_hlv_bu *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_UB, gen_helper_hyp_hlv_bu);
}

static bool trans_hlv_hu(DisasContext *ctx, arg_hlv_hu *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TEUW, gen_helper_hyp_hlv_hu);
}

static bool trans_hsv_b(DisasContext *ctx, arg_hsv_b *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hsv(ctx, a, MO_SB, gen_helper_hyp_hsv_b);
}

static bool trans_hsv_h(DisasContext *ctx, arg_hsv_h *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hsv(ctx, a, MO_TESW, gen_helper_hyp_hsv_h);
}

static bool trans_hsv_w(DisasContext *ctx, arg_hsv_w *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hsv(ctx, a, MO_TESL, gen_helper_hyp_hsv_w);
}

static bool trans_hlv_wu(DisasContext *ctx, arg_hlv_wu *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TEUL, gen_helper_hyp_hlv_wu);
}

static bool trans_hlv_d(DisasContext *ctx, arg_hlv_d *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TEUQ, gen_helper_hyp_hlv_d);
}

static bool trans_hsv_d(DisasContext *ctx, arg_hsv_d *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVH);
    return do_hsv(ctx, a, MO_TEUQ, gen_helper_hyp_hsv_d);
}

static bool trans_hlvx_hu(DisasContext *ctx, arg_hlvx_hu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    return do_hlvx(ctx, a, gen_helper_hyp_hlvx_hu);
#else
    return false;
#endif
}

static bool trans_hlvx_wu(DisasContext *ctx, arg_hlvx_wu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    return do_hlvx(ctx, a, gen_helper_hyp_hlvx_wu);
#else
    return false;
#endif
}

static bool trans_hfence_gvma(DisasContext *ctx, arg_sfence_vma *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    decode_save_opc(ctx);
    gen_helper_hyp_gvma_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}

static bool trans_hfence_vvma(DisasContext *ctx, arg_sfence_vma *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    decode_save_opc(ctx);
    gen_helper_hyp_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}

#ifdef TARGET_CHERI_RISCV_STD
/* Store cs2 via cs1 */
static bool trans_hsvc(DisasContext *ctx, arg_hsvc *a)
{
    REQUIRE_EXT(ctx, RVH);
    decode_save_opc(ctx);

    TCGv addr = get_gpr(ctx, a->cs1, EXT_NONE);
    TCGv_i32 tcg_cs1 = tcg_constant_i32(a->cs1);
    TCGv_i32 tcg_cs2 = tcg_constant_i32(a->cs2);

    gen_helper_hyp_store_cap_via_cap(cpu_env, tcg_cs2, tcg_cs1, addr);
    return true;
}

static bool trans_hlvc(DisasContext *ctx, arg_hlvc *a)
{
    REQUIRE_EXT(ctx, RVH);
    decode_save_opc(ctx);

    TCGv addr = get_gpr(ctx, a->cs1, EXT_NONE);
    TCGv_i32 tcg_cs1 = tcg_constant_i32(a->cs1);
    TCGv_i32 tcg_cd = tcg_constant_i32(a->cd);

    gen_helper_hyp_load_cap_via_cap(cpu_env, tcg_cd, tcg_cs1, addr);
    return true;
}

#endif

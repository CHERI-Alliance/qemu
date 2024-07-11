/*
 * RISC-V translation routines for the CHERI Extension.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 Alex Richardson <Alexander.Richardson@cl.cam.ac.uk>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define DO_TRANSLATE(name, gen_helper, ...)                                    \
    static bool trans_##name(DisasContext *ctx, arg_##name *a)                 \
    {                                                                          \
        return gen_helper(__VA_ARGS__, &gen_helper_##name);                    \
    }

typedef void(cheri_int_cap_helper)(TCGv, TCGv_env, TCGv_i32);
static inline bool gen_cheri_int_cap(DisasContext *ctx, int rd, int cs,
                                 cheri_int_cap_helper *gen_func)
{
    TCGv_i32 source_regnum = tcg_const_i32(cs);
    TCGv result = tcg_temp_new();
    gen_func(result, cpu_env, source_regnum);
    gen_set_gpr(ctx, rd, result);
    tcg_temp_free(result);
    tcg_temp_free_i32(source_regnum);
    return true;
}

#define TRANSLATE_INT_CAP(name)                                                \
    DO_TRANSLATE(name, gen_cheri_int_cap, ctx, a->rd, a->rs1)

typedef void(cheri_cap_cap_helper)(TCGv_env, TCGv_i32, TCGv_i32);
static inline bool gen_cheri_cap_cap(int cd, int cs,
                                     cheri_cap_cap_helper *gen_func)
{
    TCGv_i32 dest_regnum = tcg_const_i32(cd);
    TCGv_i32 source_regnum = tcg_const_i32(cs);
    gen_func(cpu_env, dest_regnum, source_regnum);
    tcg_temp_free_i32(source_regnum);
    tcg_temp_free_i32(dest_regnum);
    return true;
}
#define TRANSLATE_CAP_CAP(name)                                                \
    DO_TRANSLATE(name, gen_cheri_cap_cap, a->rd, a->rs1)

typedef void(cheri_cap_int_helper)(TCGv_env, TCGv_i32, TCGv);
static inline QEMU_ALWAYS_INLINE bool
gen_cheri_cap_int_plus_imm(DisasContext *ctx, int cd, int rs, target_long imm,
                           cheri_cap_int_helper *gen_func)
{
    TCGv_i32 dest_regnum = tcg_const_i32(cd);
    TCGv gpr_value = tcg_temp_new();
    gen_get_gpr(ctx, gpr_value, rs);
    tcg_gen_addi_tl(gpr_value, gpr_value, imm);
    gen_func(cpu_env, dest_regnum, gpr_value);
    tcg_temp_free(gpr_value);
    tcg_temp_free_i32(dest_regnum);
    return true;
}

static inline bool gen_cheri_cap_int(DisasContext *ctx, int cd, int rs,
                                     cheri_cap_int_helper *gen_func)
{
    return gen_cheri_cap_int_plus_imm(ctx, cd, rs, 0, gen_func);
}

typedef void(cheri_int_int_helper)(TCGv, TCGv_env, TCGv);
static inline bool gen_cheri_int_int(DisasContext *ctx, int rd, int rs,
                                     cheri_int_int_helper *gen_func)
{
    TCGv result = tcg_temp_new();
    TCGv gpr_src_value = tcg_temp_new();
    gen_get_gpr(ctx, gpr_src_value, rs);
    gen_func(result, cpu_env, gpr_src_value);
    gen_set_gpr(ctx, rd, result);
    tcg_temp_free(gpr_src_value);
    tcg_temp_free(result);
    return true;
}

typedef void(cheri_cap_cap_cap_helper)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32);
static inline bool gen_cheri_cap_cap_cap(int cd, int cs1, int cs2,
                                         cheri_cap_cap_cap_helper *gen_func)
{
    TCGv_i32 dest_regnum = tcg_const_i32(cd);
    TCGv_i32 source_regnum1 = tcg_const_i32(cs1);
    TCGv_i32 source_regnum2 = tcg_const_i32(cs2);
    gen_func(cpu_env, dest_regnum, source_regnum1, source_regnum2);
    tcg_temp_free_i32(source_regnum2);
    tcg_temp_free_i32(source_regnum1);
    tcg_temp_free_i32(dest_regnum);
    return true;
}
// We assume that all these instructions can trap (e.g. seal violation)
#define TRANSLATE_CAP_CAP_CAP(name)                                            \
    DO_TRANSLATE(name, gen_cheri_cap_cap_cap, a->rd, a->rs1, a->rs2)

typedef void(cheri_cap_cap_int_helper)(TCGv_env, TCGv_i32, TCGv_i32, TCGv);
static inline bool
gen_cheri_cap_cap_int_plus_imm(DisasContext *ctx, int cd, int cs1, int rs2,
                               target_long imm,
                               cheri_cap_cap_int_helper *gen_func)
{
    TCGv_i32 dest_regnum = tcg_const_i32(cd);
    TCGv_i32 source_regnum = tcg_const_i32(cs1);
    TCGv gpr_value = tcg_temp_new();
    gen_get_gpr(ctx, gpr_value, rs2);
    if (imm != 0) {
        tcg_gen_addi_tl(gpr_value, gpr_value, imm);
    }
    gen_func(cpu_env, dest_regnum, source_regnum, gpr_value);
    tcg_temp_free(gpr_value);
    tcg_temp_free_i32(source_regnum);
    tcg_temp_free_i32(dest_regnum);
    return true;
}
#define TRANSLATE_CAP_CAP_INT(name)                                            \
    DO_TRANSLATE(name, gen_cheri_cap_cap_int_plus_imm, ctx, a->rd, a->rs1,     \
                 a->rs2, 0)

typedef void(cheri_cap_loadstore_helper)(TCGv_env, TCGv_i32, TCGv, TCGv_i32);
static inline bool gen_cheri_cap_loadstore(DisasContext *ctx, int srcdst,
                                           int auth, target_long imm,
                                           cheri_cap_loadstore_helper *gen_func)
{
    TCGv_i32 srcdest_regnum = tcg_const_i32(srcdst);
    TCGv_i32 auth_regnum = tcg_const_i32(auth);
    TCGv addr = tcg_temp_new();
    gen_cap_get_cursor(ctx, auth, addr);
    if (imm != 0) {
        tcg_gen_addi_tl(addr, addr, imm);
    }
    gen_func(cpu_env, srcdest_regnum, addr, auth_regnum);
    tcg_temp_free(addr);
    tcg_temp_free_i32(auth_regnum);
    tcg_temp_free_i32(srcdest_regnum);
    return true;
}

typedef void(cheri_int_cap_cap_helper)(TCGv, TCGv_env, TCGv_i32, TCGv_i32);
static inline bool gen_cheri_int_cap_cap(DisasContext *ctx, int rd, int cs1,
                                         int cs2,
                                         cheri_int_cap_cap_helper *gen_func)
{
    TCGv_i32 source_regnum1 = tcg_const_i32(cs1);
    TCGv_i32 source_regnum2 = tcg_const_i32(cs2);
    TCGv result = tcg_temp_new();
    gen_func(result, cpu_env, source_regnum1, source_regnum2);
    gen_set_gpr(ctx, rd, result);
    tcg_temp_free(result);
    tcg_temp_free_i32(source_regnum2);
    tcg_temp_free_i32(source_regnum1);
    return true;
}
#define TRANSLATE_INT_CAP_CAP(name)                                            \
    DO_TRANSLATE(name, gen_cheri_int_cap_cap, ctx, a->rd, a->rs1, a->rs2)

// TODO: all of these could be implemented in TCG without calling a helper

// Two operand (int int)
static inline bool trans_crrl(DisasContext *ctx, arg_crrl *a)
{
    return gen_cheri_int_int(ctx, a->rd, a->rs1, &gen_helper_crap);
}
static inline bool trans_cram(DisasContext *ctx, arg_cram *a)
{
    return gen_cheri_int_int(ctx, a->rd, a->rs1, &gen_helper_cram);
}

// Three operand (cap cap int)
TRANSLATE_CAP_CAP_INT(scbnds)
TRANSLATE_CAP_CAP_INT(scbndsr)

// Three operand (int cap cap)
TRANSLATE_INT_CAP_CAP(csub)

// CIncOffsetImm/CSetBoundsImm:
typedef void(cheri_cap_cap_imm_helper)(TCGv_env, TCGv_i32, TCGv_i32, TCGv);
static inline bool gen_cheri_cap_cap_imm(int cd, int cs1, target_long imm,
                                         cheri_cap_cap_imm_helper *gen_func)
{
    TCGv_i32 dest_regnum = tcg_const_i32(cd);
    TCGv_i32 source_regnum = tcg_const_i32(cs1);
    TCGv imm_value = tcg_const_tl(imm);
    gen_func(cpu_env, dest_regnum, source_regnum, imm_value);
    tcg_temp_free(imm_value);
    tcg_temp_free_i32(source_regnum);
    tcg_temp_free_i32(dest_regnum);
    return true;
}

static bool trans_scbndsi(DisasContext *ctx, arg_scbndsi *a)
{
    tcg_debug_assert(a->imm >= 0);
    TCGv_i32 dest_regnum = tcg_const_i32(a->cd);
    TCGv_i32 source_regnum = tcg_const_i32(a->cs1);
    TCGv_i32 imm_value = tcg_const_i32(a->imm);
    TCGv_i32 scale_val = tcg_const_i32(a->scal);

    gen_helper_scbndsi(cpu_env, dest_regnum, source_regnum, imm_value, scale_val );
    tcg_temp_free_i32(imm_value);
    tcg_temp_free_i32(scale_val);
    tcg_temp_free_i32(source_regnum);
    tcg_temp_free_i32(dest_regnum);
    return true;
}

/// Control-flow instructions
static void gen_cjal(DisasContext *ctx, int rd, target_ulong imm)
{
    /* Mask the LSB to match the RISC-V spec for JAL. */
    target_ulong next_pc = (ctx->base.pc_next + imm) & ~(target_ulong)1;

    TCGv_i32 dst = tcg_const_i32(rd);
    TCGv target_addr = tcg_const_tl(next_pc);
    TCGv immv = tcg_const_tl(imm);
    TCGv link_addr = tcg_const_tl(ctx->pc_succ_insn);
    /*
     * helper_cjal() checks for misalignment and capability bounds. It also
     * ensures that the written value is a sentry and marks PC as up-to-date.
     */
    gen_helper_cjal(cpu_env, dst, target_addr, link_addr);
    tcg_temp_free(link_addr);
    tcg_temp_free(immv);
    tcg_temp_free(target_addr);
    tcg_temp_free_i32(dst);

    /* No bounds check needed here since we did it in helper_cjal(). */
    gen_goto_tb(ctx, 0, next_pc, false);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_cjalr(DisasContext *ctx, int rd, int rs1, target_ulong imm)
{
    TCGv_i32 dest_regnum = tcg_const_i32(rd);
    TCGv_i32 source_regnum = tcg_const_i32(rs1);
    TCGv imm_value = tcg_const_tl(imm);
    TCGv t0 = tcg_const_tl(ctx->pc_succ_insn); // Link addr + resulting $pc
    gen_helper_cjalr(cpu_env, dest_regnum, source_regnum, imm_value, t0);
    tcg_temp_free(imm_value);
    tcg_temp_free(t0);
    tcg_temp_free_i32(source_regnum);
    tcg_temp_free_i32(dest_regnum);

    tcg_gen_lookup_and_goto_ptr();
    // PC has been updated -> exit translation block
    ctx->base.is_jmp = DISAS_NORETURN;
}

static inline bool gen_cap_load_mem_idx(DisasContext *ctx, int32_t rd,
                                        int32_t cs, target_long offset,
                                        int mem_idx, MemOp op)
{
    // FIXME: just do everything in the helper
    TCGv value = tcg_temp_new();
    TCGv_cap_checked_ptr vaddr = tcg_temp_new_cap_checked();
    generate_cap_load_check_imm(vaddr, cs, offset, op);
    tcg_gen_qemu_ld_tl_with_checked_addr(value, vaddr, mem_idx, op);
    gen_set_gpr(ctx, rd, value);
    tcg_temp_free_cap_checked(vaddr);
    tcg_temp_free(value);
    return true;
}

static inline bool gen_cap_load(DisasContext *ctx, int32_t rd, int32_t cs,
    target_long offset, MemOp op)
{
    return gen_cap_load_mem_idx(ctx, rd, cs, offset, ctx->mem_idx, op);
}

static inline bool trans_lc(DisasContext *ctx, arg_lc *a)
{
    if (!ctx->capmode) {
        // Without capmode we load relative to DDC (lc instructions)
        return gen_cheri_cap_int_plus_imm(ctx, a->rd, a->rs1, a->imm,
                                          &gen_helper_load_cap_via_ddc);
    }
    return gen_cheri_cap_loadstore(ctx, a->rd, a->rs1, /*offset=*/a->imm,
                                   &gen_helper_load_cap_via_cap);
}

/* Load Via Capability Register */
static inline bool gen_cap_store_mem_idx(DisasContext *ctx, int32_t addr_regnum,
                                         int32_t val_regnum, target_long offset,
                                         int mem_idx, MemOp op)
{
    // FIXME: just do everything in the helper
    TCGv_cap_checked_ptr vaddr = tcg_temp_new_cap_checked();
    generate_cap_store_check_imm(vaddr, addr_regnum, offset, op);

    TCGv value = tcg_temp_new();
    gen_get_gpr(ctx, value, val_regnum);
    tcg_gen_qemu_st_tl_with_checked_addr(value, vaddr, mem_idx, op);
    tcg_temp_free(value);
    tcg_temp_free_cap_checked(vaddr);
    return true;
}

static inline bool gen_cap_store(DisasContext *ctx, int32_t addr_regnum,
                                 int32_t val_regnum, target_long offset,
                                 MemOp op)
{
    gen_cap_store_mem_idx(ctx, addr_regnum, val_regnum, offset, ctx->mem_idx,
                          op);
    return true;
}

static inline bool trans_sc(DisasContext *ctx, arg_sc *a)
{
    // RS2 is the value, RS1 is the capability
    if (!ctx->capmode) {
        // Without capmode we store relative to DDC (sc instructions)
        return gen_cheri_cap_int_plus_imm(ctx, a->rs2, a->rs1, a->imm,
                                          &gen_helper_store_cap_via_ddc);
    }
    return gen_cheri_cap_loadstore(ctx, a->rs2, a->rs1, /*offset=*/a->imm,
                                   &gen_helper_store_cap_via_cap);
}

// Atomic ops
static inline bool trans_lr_c_impl(DisasContext *ctx, arg_atomic *a,
                                   cheri_cap_cap_helper *helper)
{
    REQUIRE_EXT(ctx, RVA);
    if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
        // In a parallel context, stop the world and single step.
        gen_helper_exit_atomic(cpu_env);
        ctx->base.is_jmp = DISAS_NORETURN;
    } else {
        // Note: we ignore the Acquire/release flags since using
        // helper_exit_atomic forces exlusive execution so we get SC semantics.
        tcg_debug_assert(a->rs2 == 0);
        gen_cheri_cap_cap(a->rd, a->rs1, helper);
    }
    return true;
}

static inline bool trans_lr_c(DisasContext *ctx, arg_lr_c *a)
{
    // Note: The capmode dependent address interpretation happens in the
    // helper and not during translation.
    return trans_lr_c_impl(ctx, a, &gen_helper_lr_c_modedep);
}

static inline bool trans_lr_c_ddc(DisasContext *ctx, arg_lr_c_ddc *a)
{
    return trans_lr_c_impl(ctx, a, &gen_helper_lr_c_ddc);
}

static inline bool trans_lr_c_cap(DisasContext *ctx, arg_lr_c_cap *a)
{
    return trans_lr_c_impl(ctx, a, &gen_helper_lr_c_cap);
}

static inline bool trans_sc_c_impl(DisasContext *ctx, arg_atomic *a,
                                   cheri_int_cap_cap_helper *helper)
{
    REQUIRE_EXT(ctx, RVA);
    if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
        // In a parallel context, stop the world and single step.
        gen_helper_exit_atomic(cpu_env);
        ctx->base.is_jmp = DISAS_NORETURN;
    } else {
        // Note: we ignore the Acquire/release flags since using
        // helper_exit_atomic forces exclusive execution so we get SC semantics.
        gen_cheri_int_cap_cap(ctx, a->rd, a->rs1, a->rs2, helper);
    }
    return true;
}

static inline bool trans_sc_c(DisasContext *ctx, arg_sc_c *a)
{
    // Note: The capmode dependent address interpretation happens in the
    // helper and not during translation.
    return trans_sc_c_impl(ctx, a, &gen_helper_sc_c_modedep);
}

static inline bool trans_sc_c_ddc(DisasContext *ctx, arg_sc_c_ddc *a)
{
    a->rd = a->rs2; /* Not enough encoding space for explicit rd */
    return trans_sc_c_impl(ctx, a, &gen_helper_sc_c_ddc);
}

static inline bool trans_sc_c_cap(DisasContext *ctx, arg_sc_c_cap *a)
{
    a->rd = a->rs2; /* Not enough encoding space for explicit rd */
    return trans_sc_c_impl(ctx, a, &gen_helper_sc_c_cap);
}

static inline bool trans_amoswap_c(DisasContext *ctx, arg_amoswap_c *a)
{
    REQUIRE_EXT(ctx, RVA);
    if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
        // In a parallel context, stop the world and single step.
        gen_helper_exit_atomic(cpu_env);
        ctx->base.is_jmp = DISAS_NORETURN;
    } else {
        // Note: we ignore the Acquire/release flags since using
        // helper_exit_atomic forces exlusive execution so we get SC semantics.
        gen_cheri_cap_cap_cap(a->rd, a->rs1, a->rs2, &gen_helper_amoswap_cap);
    }
    return true;
}

static inline bool do_trans_modesw(DisasContext *ctx, bool to_capmode)
{
    if (ctx->capmode == to_capmode) {
        qemu_log_mask_and_addr(CPU_LOG_INSTR, ctx->base.pc_first,
                               "Redundant modesw at " TARGET_FMT_lx " (%s)",
                               ctx->base.pc_first,
                               lookup_symbol(ctx->base.pc_first));
        return true;
    }
    gen_helper_modesw(cpu_env, tcg_const_i32(to_capmode));

    /*
     * The current execution mode is checked at translation time (not at
     * execution time) and is part of the translation block flags, so these
     * flags are no longer correct after executing modesw -> we have to process
     * the mode update and exit the translation block.
     *
     * Note: Translating the same code with a different initial mode will never
     * find a cached block with incorrect initial mode since the mode is part of
     * the TB flags and they are used as part of the hash table lookup.
     *
     * TODO: it should be possible to update the flag in DisasContext and
     * continue translation after the modesw, but I am not confident this will
     * be correct. Once we have some unit tests that we can run we should try
     * to make this change since it will improve the performance of hybrid code.
     * See Morello code which uses DISAS_UPDATE_EXIT.
     */
    /* create tcg instruction to exit the tb */
    gen_update_cpu_pc(ctx->pc_succ_insn);
    tcg_gen_exit_tb(NULL, 0);
    /* This indicates to riscv_tr_tb_stop that no cleanup is needed. */
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static inline bool trans_modesw_cap(DisasContext *ctx, arg_modesw_cap *a)
{
    return do_trans_modesw(ctx, /*to_capmode=*/true);
}

static inline bool trans_modesw_int(DisasContext *ctx, arg_modesw_int *a)
{
    return do_trans_modesw(ctx, /*to_capmode=*/false);
}

// Explicit CAP/DDC atomic ops (no unsigned versions):
// Reuses gen_lr_impl, defined in trans_rva.c.inc
static inline bool gen_lr_impl(DisasContext *ctx, TCGv_cap_checked_ptr addr,
                               arg_atomic *a, MemOp mop);

#define TRANSLATE_LR_EXPLICIT(name, op)                                        \
    static bool trans_##name##_ddc(DisasContext *ctx, arg_##name##_ddc *a)     \
    {                                                                          \
        REQUIRE_EXT(ctx, RVA);                                                 \
        TCGv_cap_checked_ptr addr = tcg_temp_new_cap_checked();                \
        generate_get_ddc_checked_gpr_plus_offset(                              \
            addr, ctx, a->rs1, 0, op, &generate_ddc_checked_load_ptr);         \
        bool result = gen_lr_impl(ctx, addr, a, op);                           \
        tcg_temp_free_cap_checked(addr);                                       \
        return result;                                                         \
    }                                                                          \
    static bool trans_##name##_cap(DisasContext *ctx, arg_##name##_cap *a)     \
    {                                                                          \
        REQUIRE_EXT(ctx, RVA);                                                 \
        TCGv_cap_checked_ptr addr = tcg_temp_new_cap_checked();                \
        generate_cap_load_check_imm(addr, a->rs1, 0, op);                      \
        bool result = gen_lr_impl(ctx, addr, a, op);                           \
        tcg_temp_free_cap_checked(addr);                                       \
        return result;                                                         \
    }
TRANSLATE_LR_EXPLICIT(lr_w, MO_ALIGN | MO_SL);
#ifdef TARGET_RISCV64
TRANSLATE_LR_EXPLICIT(lr_d, MO_ALIGN | MO_Q);
#endif

static inline bool gen_sc_impl(DisasContext *ctx, TCGv_cap_checked_ptr addr,
                               arg_atomic *a, MemOp mop);

#define TRANSLATE_SC_EXPLICIT(name, op)                                        \
    static bool trans_##name##_ddc(DisasContext *ctx, arg_##name##_ddc *a)     \
    {                                                                          \
        REQUIRE_EXT(ctx, RVA);                                                 \
        TCGv_cap_checked_ptr addr = tcg_temp_new_cap_checked();                \
        generate_get_ddc_checked_gpr_plus_offset(                              \
            addr, ctx, a->rs1, 0, op, &generate_ddc_checked_load_ptr);         \
        a->rd = a->rs2; /* Not enough encoding space for explicit rd */        \
        bool result = gen_sc_impl(ctx, addr, a, op);                           \
        tcg_temp_free_cap_checked(addr);                                       \
        return result;                                                         \
    }                                                                          \
    static bool trans_##name##_cap(DisasContext *ctx, arg_##name##_cap *a)     \
    {                                                                          \
        REQUIRE_EXT(ctx, RVA);                                                 \
        TCGv_cap_checked_ptr addr = tcg_temp_new_cap_checked();                \
        generate_cap_load_check_imm(addr, a->rs1, 0, op);                      \
        a->rd = a->rs2; /* Not enough encoding space for explicit rd */        \
        bool result = gen_sc_impl(ctx, addr, a, op);                           \
        tcg_temp_free_cap_checked(addr);                                       \
        return result;                                                         \
    }
TRANSLATE_SC_EXPLICIT(sc_b, MO_ALIGN | MO_SB);
TRANSLATE_SC_EXPLICIT(sc_h, MO_ALIGN | MO_SW);
TRANSLATE_SC_EXPLICIT(sc_w, MO_ALIGN | MO_SL);
#ifdef TARGET_RISCV64
TRANSLATE_SC_EXPLICIT(sc_d, MO_ALIGN | MO_Q);
#endif

/*
 * The Bakewell specification uses rd and cs1 for the parameter names,
 * it's helpful to have the same names in the code instead of reusing
 * TRANSLATE_INT_CAP.
 */
#define TRANSLATE_INT_CAP_BW(name) \
    DO_TRANSLATE(name, gen_cheri_int_cap, ctx, a->rd, a->cs1)

#define TRANSLATE_CAP_CAP_IMM_BW(name) \
    DO_TRANSLATE(name, gen_cheri_cap_cap_imm, a->cd, a->cs1, a->imm)

#define TRANSLATE_CAP_CAP_INT_BW(name) \
    DO_TRANSLATE(name, gen_cheri_cap_cap_int_plus_imm, a->cd, a->cs1, a->rs2, 0)

#define TRANSLATE_INT_CAP_CAP_BW(name) \
    DO_TRANSLATE(name, gen_cheri_int_cap_cap, ctx, a->rd, a->cs1, a->cs2)

#define TRANSLATE_CAP_CAP_BW(name) \
    DO_TRANSLATE(name, gen_cheri_cap_cap, a->cd, a->cs1)

#define TRANSLATE_CAP_CAP_CAP_BW(name) \
    DO_TRANSLATE(name, gen_cheri_cap_cap_cap, a->cd, a->cs1, a->cs2)

TRANSLATE_INT_CAP_BW(gctag)
TRANSLATE_INT_CAP_BW(gcperm)
TRANSLATE_INT_CAP_BW(gchi)
TRANSLATE_INT_CAP_BW(gcbase)
TRANSLATE_INT_CAP_BW(gclen)

static bool trans_cadd(DisasContext *ctx, arg_cadd *a)
{
    TCGv_i32 dest_regnum;
    TCGv_i32 source_regnum;
    TCGv gpr_value;

    /*
     * Handle the special cases here in tcg only if we're in capability
     * pointer mode, i.e. if the cadd instruction is valid.
     * If we're in integer pointer mode, we go into the helper and throw an
     * exception.
     * TODO: Update the check for capability pointer mode to take the CPU
     * execution mode and the CRE bits into account (here and elsewhere).
     */
    if (ctx->capmode) {
        if (a->cd == 0) {
            /*
             * cadd cd, cs1, rs1 increments cs1's address by rs1 and stores
             * the result in cd. If cd == c0, the result is discarded, the
             * calculation has no side effects. There's nothing to do.
             */
            return true;
        }

        if (a->rs2 == 0) {
            /*
             * cadd cd, cs1, x0 copies cs1 to cd. cd's tag is not cleared if
             * cs1 is sealed.
             * The copy can be done on the tcg layer without a helper.
             */

            gen_ensure_cap_decompressed(ctx, a->cs1);
            /*
             * This copies the complete cap_register_t, including the
             * compressed representation and the decompressed fields such as
             * cr_arch_perm.
             */
            gen_move_cap(gp_register_offset(a->cd), gp_register_offset(a->cs1));

#ifdef ENABLE_STATIC_CAP_OPTS
#error "Review the cap register copy operation. " \
       "Do we have to call disas_capreg_state_set?"
#endif

            return true;
        }
    }

    dest_regnum = tcg_const_i32(a->cd);
    source_regnum = tcg_const_i32(a->cs1);
    gpr_value = tcg_temp_new();
    gen_get_gpr(gpr_value, a->rs2);

    gen_helper_caddi(cpu_env, dest_regnum, source_regnum, gpr_value);

    tcg_temp_free(gpr_value);
    tcg_temp_free_i32(source_regnum);
    tcg_temp_free_i32(dest_regnum);
    return true;
}

TRANSLATE_CAP_CAP_IMM_BW(caddi)

TRANSLATE_CAP_CAP_INT_BW(scaddr)
TRANSLATE_CAP_CAP_INT_BW(schi)
TRANSLATE_INT_CAP_CAP_BW(sceq)
TRANSLATE_CAP_CAP_INT_BW(acperm)
TRANSLATE_CAP_CAP_CAP_BW(cbld)
TRANSLATE_INT_CAP_CAP_BW(scss)

TRANSLATE_CAP_CAP_BW(sentry)

TRANSLATE_CAP_CAP_INT_BW(scmode)

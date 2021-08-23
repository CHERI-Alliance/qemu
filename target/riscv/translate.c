/*
 * RISC-V emulation for qemu: main translation routines.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "disas/disas.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"
#include "exec/log_instr.h"

#include "instmap.h"


/* global register indices */
#ifdef TARGET_CHERI
#include "cheri-lazy-capregs.h"
static TCGv _cpu_cursors_do_not_access_directly[32];
static TCGv cpu_pc;  // Note: this is PCC.cursor
#else
static TCGv cpu_gpr[32], cpu_pc;
#endif
#ifdef CONFIG_RVFI_DII
TCGv_i32 cpu_rvfi_available_fields;
#endif
static TCGv cpu_vl;
static TCGv_i64 cpu_fpr[32]; /* assume F and D extensions */
static TCGv_cap_checked_ptr load_res;
static TCGv load_val;

#include "exec/gen-icount.h"

/*
 * If an operation is being performed on less than TARGET_LONG_BITS,
 * it may require the inputs to be sign- or zero-extended; which will
 * depend on the exact operation being performed.
 */
typedef enum {
    EXT_NONE,
    EXT_SIGN,
    EXT_ZERO,
} DisasExtend;

typedef struct DisasContext {
    DisasContextBase base;
    /* pc_succ_insn points to the instruction following base.pc_next */
    target_ulong pc_succ_insn;
    target_ulong priv_ver;
    target_ulong misa;
    uint32_t opcode;
    uint32_t mstatus_fs;
    uint32_t mem_idx;
    /* Remember the rounding mode encoded in the previous fp instruction,
       which we have already installed into env->fp_status.  Or -1 for
       no previous fp instruction.  Note that we exit the TB when writing
       to any system register, which includes CSR_FRM, so we do not have
       to reset this known value.  */
    int frm;
    bool w;
    bool virt_enabled;
    bool ext_ifencei;
    bool hlsx;
#ifdef TARGET_CHERI
    bool capmode;
    bool hybrid;
    bool cre;
#endif
    /* vector extension */
    bool vill;
    uint8_t lmul;
    uint8_t sew;
    uint16_t vlen;
    uint16_t mlen;
    bool vl_eq_vlmax;
    uint8_t ntemp;
    CPUState *cs;
    TCGv zero;
    /* Space for 3 operands plus 1 extra for address computation. */
    TCGv temp[4];
} DisasContext;

#ifdef CONFIG_DEBUG_TCG
#define gen_mark_pc_updated() tcg_gen_movi_tl(_pc_is_current, 1)
#else
#define gen_mark_pc_updated() ((void)0)
#endif


static inline void gen_update_cpu_pc(target_ulong new_pc)
{
    tcg_gen_movi_tl(cpu_pc, new_pc);
    gen_mark_pc_updated();
}

static inline bool has_ext(DisasContext *ctx, uint32_t ext)
{
    return ctx->misa & ext;
}

#ifdef TARGET_RISCV32
# define is_32bit(ctx)  true
#elif defined(CONFIG_USER_ONLY)
# define is_32bit(ctx)  false
#else
static inline bool is_32bit(DisasContext *ctx)
{
    return (ctx->misa & RV32) == RV32;
}
#endif

/*
 * RISC-V requires NaN-boxing of narrower width floating point values.
 * This applies when a 32-bit value is assigned to a 64-bit FP register.
 * For consistency and simplicity, we nanbox results even when the RVD
 * extension is not present.
 */
static void gen_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(32, 32));
}

/*
 * A narrow n-bit operation, where n < FLEN, checks that input operands
 * are correctly Nan-boxed, i.e., all upper FLEN - n bits are 1.
 * If so, the least-significant bits of the input are used, otherwise the
 * input value is treated as an n-bit canonical NaN (v2.2 section 9.2).
 *
 * Here, the result is always nan-boxed, even the canonical nan.
 */
static void gen_check_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    TCGv_i64 t_max = tcg_constant_i64(0xffffffff00000000ull);
    TCGv_i64 t_nan = tcg_constant_i64(0xffffffff7fc00000ull);

    tcg_gen_movcond_i64(TCG_COND_GEU, out, in, t_max, in, t_nan);
}

static void generate_exception(DisasContext *ctx, int excp)
{
    gen_update_cpu_pc(ctx->base.pc_next);
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(excp));
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void generate_exception_mtval(DisasContext *ctx, int excp)
{
    gen_update_cpu_pc(ctx->base.pc_next);
    tcg_gen_st_tl(cpu_pc, cpu_env, offsetof(CPURISCVState, badaddr));
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(excp));
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_debug(void)
{
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(EXCP_DEBUG));
}

/* Wrapper around tcg_gen_exit_tb that handles single stepping */
static void exit_tb(DisasContext *ctx)
{
    if (ctx->base.singlestep_enabled) {
        gen_exception_debug();
    } else {
        tcg_gen_exit_tb(NULL, 0);
    }
}

/* Wrapper around tcg_gen_lookup_and_goto_ptr that handles single stepping */
static void lookup_and_goto_ptr(DisasContext *ctx)
{
    if (ctx->base.singlestep_enabled) {
        gen_exception_debug();
    } else {
        tcg_gen_lookup_and_goto_ptr();
    }
}

static void gen_exception_illegal(DisasContext *ctx)
{
    generate_exception(ctx, RISCV_EXCP_ILLEGAL_INST);
}

static void gen_exception_inst_addr_mis(DisasContext *ctx)
{
    generate_exception_mtval(ctx, RISCV_EXCP_INST_ADDR_MIS);
}

/*
 * Wrappers for getting reg values.
 *
 * The $zero register does not have cpu_gpr[0] allocated -- we supply the
 * constant zero as a source, and an uninitialized sink as destination.
 *
 * Further, we may provide an extension for word operations.
 */
static TCGv temp_new(DisasContext *ctx)
{
    assert(ctx->ntemp < ARRAY_SIZE(ctx->temp));
    return ctx->temp[ctx->ntemp++] = tcg_temp_new();
}

static TCGv get_gpr(DisasContext *ctx, int reg_num, DisasExtend ext)
{
    TCGv i, t;

    if (reg_num == 0) {
        return ctx->zero;
    }

#ifdef TARGET_CHERI
    i = _cpu_cursors_do_not_access_directly[reg_num];
#else
    i = cpu_gpr[reg_num];
#endif

    switch (ctx->w ? ext : EXT_NONE) {
    case EXT_NONE:
        return i;
    case EXT_SIGN:
        t = temp_new(ctx);
        tcg_gen_ext32s_tl(t, i);
        return t;
    case EXT_ZERO:
        t = temp_new(ctx);
        tcg_gen_ext32u_tl(t, i);
        return t;
    }
    g_assert_not_reached();
}

/* Wrapper for getting reg values - need to check of reg is zero since
 * cpu_gpr[0] is not actually allocated
 */
static inline void gen_get_gpr(DisasContext *ctx, TCGv t, int reg_num)
{
    if (reg_num == 0) {
        tcg_gen_movi_tl(t, 0);
    } else {
        tcg_gen_mov_tl(t, get_gpr(ctx, reg_num, EXT_NONE));
    }
}

#include "cheri-translate-utils.h"

static TCGv dest_gpr(DisasContext *ctx, int reg_num)
{
    if (reg_num == 0 || ctx->w) {
        return temp_new(ctx);
    }
#ifdef TARGET_CHERI
    return _cpu_cursors_do_not_access_directly[reg_num];
#else
    return cpu_gpr[reg_num];
#endif
}


/* Wrapper for setting reg values - need to check of reg is zero since
 * cpu_gpr[0] is not actually allocated. this is more for safety purposes,
 * since we usually avoid calling the OP_TYPE_gen function if we see a write to
 * $zero
 */
static inline void _gen_set_gpr(DisasContext *ctx, int reg_num_dst, TCGv t,
                                bool clear_pesbt)
{
    TCGv r;
    if (reg_num_dst != 0) {
#ifdef TARGET_CHERI
        if (clear_pesbt) {
            gen_lazy_cap_set_int(ctx, reg_num_dst); // Reset the register type to int.
        }
        r = _cpu_cursors_do_not_access_directly[reg_num_dst];
#else
        r = cpu_gpr[reg_num_dst];
#endif
        if (ctx->w) {
            tcg_gen_ext32s_tl(r, t);
        } else {
            tcg_gen_mov_tl(r, t);
        }
        gen_rvfi_dii_set_field_const_i8(INTEGER, rd_addr, reg_num_dst);
        gen_rvfi_dii_set_field_zext_tl(INTEGER, rd_wdata, t);
#ifdef CONFIG_TCG_LOG_INSTR
        // Log GPR writes here
        if (unlikely(ctx->base.log_instr_enabled)) {
            TCGv_i32 tregnum = tcg_const_i32(reg_num_dst);
            gen_helper_riscv_log_gpr_write(cpu_env, tregnum, t);
            tcg_temp_free_i32(tregnum);
        }
#endif
    }
}

static inline void gen_set_gpr_const(DisasContext *ctx, int reg_num_dst,
                                      target_ulong value)
{
    TCGv r;
    if (reg_num_dst != 0) {
#ifdef TARGET_CHERI
        gen_lazy_cap_set_int(ctx, reg_num_dst); // Reset the register type to int.
        r = _cpu_cursors_do_not_access_directly[reg_num_dst];
#else
        r = cpu_gpr[reg_num_dst];
#endif
        if (ctx->w) {
            TCGv t = tcg_const_local_tl(value);
            tcg_gen_ext32s_tl(r, t);
        } else {
            tcg_gen_movi_tl(r, value);
        }
        gen_rvfi_dii_set_field_const_i8(INTEGER, rd_addr, reg_num_dst);
        gen_rvfi_dii_set_field_const_i64(INTEGER, rd_wdata, value);
#ifdef CONFIG_TCG_LOG_INSTR
        // Log GPR writes here
        if (unlikely(ctx->base.log_instr_enabled)) {
            TCGv_i32 tregnum = tcg_const_i32(reg_num_dst);
            TCGv tval = tcg_const_tl(value);
            gen_helper_riscv_log_gpr_write(cpu_env, tregnum, tval);
            tcg_temp_free(tval);
            tcg_temp_free_i32(tregnum);
        }
#endif
    }
}

#define gen_set_gpr(ctx, reg_num_dst, t) _gen_set_gpr(ctx, reg_num_dst, t, true)

#ifdef CONFIG_TCG_LOG_INSTR
static inline void gen_riscv_log_instr(DisasContext *ctx, uint32_t opcode,
                                       int width)
{
    if (unlikely(ctx->base.log_instr_enabled)) {
        TCGv tpc = tcg_const_tl(ctx->base.pc_next);
        TCGv_i32 topc = tcg_const_i32(opcode);
        TCGv_i32 twidth = tcg_const_i32(width);
        // TODO(am2419): bswap opcode if target byte-order != host byte-order
        gen_helper_riscv_log_instr(cpu_env, tpc, topc, twidth);
        tcg_temp_free(tpc);
        tcg_temp_free_i32(topc);
        tcg_temp_free_i32(twidth);
    }
}

#else /* ! CONFIG_TCG_LOG_INSTR */
#define gen_riscv_log_instr(ctx, opcode, width) ((void)0)
#endif /* ! CONFIG_TCG_LOG_INSTR */

#define gen_riscv_log_instr16(ctx, opcode)              \
    gen_riscv_log_instr(ctx, opcode, sizeof(uint16_t))
#define gen_riscv_log_instr32(ctx, opcode)              \
    gen_riscv_log_instr(ctx, opcode, sizeof(uint32_t))

void cheri_tcg_save_pc(DisasContextBase *db) { gen_update_cpu_pc(db->pc_next); }
// We have to call gen_update_cpu_pc() before setting DISAS_NORETURN (see
// generate_exception())
void cheri_tcg_prepare_for_unconditional_exception(DisasContextBase *db)
{
    cheri_tcg_save_pc(db);
    db->is_jmp = DISAS_NORETURN;
}

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest,
                        bool bounds_check)
{
    if (bounds_check)
        gen_check_branch_target(ctx, dest);

    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(n);
        gen_update_cpu_pc(dest);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        gen_update_cpu_pc(dest);
        lookup_and_goto_ptr(ctx);
    }
}

static void gen_mulhsu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv rl = tcg_temp_new();
    TCGv rh = tcg_temp_new();

    tcg_gen_mulu2_tl(rl, rh, arg1, arg2);
    /* fix up for one negative */
    tcg_gen_sari_tl(rl, arg1, TARGET_LONG_BITS - 1);
    tcg_gen_and_tl(rl, rl, arg2);
    tcg_gen_sub_tl(ret, rh, rl);

    tcg_temp_free(rl);
    tcg_temp_free(rh);
}

static void gen_div(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, mone, min;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);
    min = tcg_constant_tl(1ull << (TARGET_LONG_BITS - 1));

    /*
     * If overflow, set temp2 to 1, else source2.
     * This produces the required result of min.
     */
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, source1, min);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, mone);
    tcg_gen_and_tl(temp1, temp1, temp2);
    tcg_gen_movcond_tl(TCG_COND_NE, temp2, temp1, zero, one, source2);

    /*
     * If div by zero, set temp1 to -1 and temp2 to 1 to
     * produce the required result of -1.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp1, source2, zero, mone, source1);
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, temp2);

    tcg_gen_div_tl(ret, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static void gen_divu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, max;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    max = tcg_constant_tl(~0);

    /*
     * If div by zero, set temp1 to max and temp2 to 1 to
     * produce the required result of max.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp1, source2, zero, max, source1);
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, source2);
    tcg_gen_divu_tl(ret, temp1, temp2);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static void gen_rem(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp1, temp2, zero, one, mone, min;

    temp1 = tcg_temp_new();
    temp2 = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);
    mone = tcg_constant_tl(-1);
    min = tcg_constant_tl(1ull << (TARGET_LONG_BITS - 1));

    /*
     * If overflow, set temp1 to 0, else source1.
     * This avoids a possible host trap, and produces the required result of 0.
     */
    tcg_gen_setcond_tl(TCG_COND_EQ, temp1, source1, min);
    tcg_gen_setcond_tl(TCG_COND_EQ, temp2, source2, mone);
    tcg_gen_and_tl(temp1, temp1, temp2);
    tcg_gen_movcond_tl(TCG_COND_NE, temp1, temp1, zero, zero, source1);

    /*
     * If div by zero, set temp2 to 1, else source2.
     * This avoids a possible host trap, but produces an incorrect result.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp2, source2, zero, one, source2);

    tcg_gen_rem_tl(temp1, temp1, temp2);

    /* If div by zero, the required result is the original dividend. */
    tcg_gen_movcond_tl(TCG_COND_EQ, ret, source2, zero, source1, temp1);

    tcg_temp_free(temp1);
    tcg_temp_free(temp2);
}

static void gen_remu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv temp, zero, one;

    temp = tcg_temp_new();
    zero = tcg_constant_tl(0);
    one = tcg_constant_tl(1);

    /*
     * If div by zero, set temp to 1, else source2.
     * This avoids a possible host trap, but produces an incorrect result.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, temp, source2, zero, one, source2);

    tcg_gen_remu_tl(temp, source1, temp);

    /* If div by zero, the required result is the original dividend. */
    tcg_gen_movcond_tl(TCG_COND_EQ, ret, source2, zero, source1, temp);

    tcg_temp_free(temp);
}

static void gen_jal(DisasContext *ctx, int rd, target_ulong imm)
{
    target_ulong next_pc;

    /* check misaligned: */
    next_pc = ctx->base.pc_next + imm;
    gen_check_branch_target(ctx, next_pc);

    if (!has_ext(ctx, RVC)) {
        if ((next_pc & 0x3) != 0) {
            gen_exception_inst_addr_mis(ctx);
            return;
        }
    }
    /* For CHERI ISAv8 the result is an offset relative to PCC.base */
    gen_set_gpr_const(ctx, rd, ctx->pc_succ_insn - pcc_reloc(ctx));

    gen_goto_tb(ctx, 0, ctx->base.pc_next + imm, /*bounds_check=*/true); /* must use this for safety */
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_jalr(DisasContext *ctx, int rd, int rs1, target_ulong imm)
{
    /* no chaining with JALR */
    TCGLabel *misaligned = NULL;
    // Note: We need to use tcg_temp_local_new() for t0 since
    // gen_check_branch_target_dynamic() inserts branches.
    TCGv t0 = tcg_temp_local_new();

    gen_get_gpr(ctx, t0, rs1);
    /* For CHERI ISAv8 the destination is an offset relative to PCC.base. */
    tcg_gen_addi_tl(t0, t0, imm + pcc_reloc(ctx));
    tcg_gen_andi_tl(t0, t0, (target_ulong)-2);
    gen_check_branch_target_dynamic(ctx, t0);
    // Note: Only update cpu_pc after a successful bounds check to avoid
    // representability issues caused by directly modifying PCC.cursor.
    tcg_gen_mov_tl(cpu_pc, t0);
    gen_mark_pc_updated();

    if (!has_ext(ctx, RVC)) {
        misaligned = gen_new_label();
        tcg_gen_andi_tl(t0, cpu_pc, 0x2);
        tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0x0, misaligned);
    }

    /* For CHERI ISAv8 the result is an offset relative to PCC.base */
    gen_set_gpr_const(ctx, rd, ctx->pc_succ_insn - pcc_reloc(ctx));
    lookup_and_goto_ptr(ctx);

    if (misaligned) {
        gen_set_label(misaligned);
        gen_exception_inst_addr_mis(ctx);
    }
    ctx->base.is_jmp = DISAS_NORETURN;

    tcg_temp_free(t0);
}

#ifndef CONFIG_USER_ONLY
/* The states of mstatus_fs are:
 * 0 = disabled, 1 = initial, 2 = clean, 3 = dirty
 * We will have already diagnosed disabled state,
 * and need to turn initial/clean into dirty.
 */
static void mark_fs_dirty(DisasContext *ctx)
{
    TCGv tmp;
    target_ulong sd;

    if (ctx->mstatus_fs == MSTATUS_FS) {
        return;
    }
    /* Remember the state change for the rest of the TB.  */
    ctx->mstatus_fs = MSTATUS_FS;

    tmp = tcg_temp_new();
    sd = is_32bit(ctx) ? MSTATUS32_SD : MSTATUS64_SD;

    tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus));
    tcg_gen_ori_tl(tmp, tmp, MSTATUS_FS | sd);
    tcg_gen_st_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus));

    if (ctx->virt_enabled) {
        tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus_hs));
        tcg_gen_ori_tl(tmp, tmp, MSTATUS_FS | sd);
        tcg_gen_st_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus_hs));
    }
    tcg_temp_free(tmp);
}
#else
static inline void mark_fs_dirty(DisasContext *ctx) { }
#endif

static void gen_set_rm(DisasContext *ctx, int rm)
{
    if (ctx->frm == rm) {
        return;
    }
    ctx->frm = rm;
    gen_helper_set_rounding_mode(cpu_env, tcg_constant_i32(rm));
}

static int ex_plus_1(DisasContext *ctx, int nf)
{
    return nf + 1;
}

#define EX_SH(amount) \
    static int ex_shift_##amount(DisasContext *ctx, int imm) \
    {                                         \
        return imm << amount;                 \
    }
EX_SH(1)
EX_SH(2)
EX_SH(3)
EX_SH(4)
EX_SH(12)

#define REQUIRE_EXT(ctx, ext) do { \
    if (!has_ext(ctx, ext)) {      \
        return false;              \
    }                              \
} while (0)

#define REQUIRE_64BIT(ctx) do { \
    if (is_32bit(ctx)) {        \
        return false;           \
    }                           \
} while (0)

static int ex_rvc_register(DisasContext *ctx, int reg)
{
    return 8 + reg;
}

static int ex_rvc_shifti(DisasContext *ctx, int imm)
{
    /* For RV128 a shamt of 0 means a shift by 64. */
    return imm ? imm : 64;
}

static bool pred_rv64(DisasContext *ctx)
{
    return !is_32bit(ctx);
}

static bool pred_capmode(DisasContext *ctx)
{
#ifdef TARGET_CHERI
    return ctx->capmode;
#else
    return false;
#endif
}

#ifdef TARGET_CHERI
static bool pred_hybrid(DisasContext *ctx)
{
    return ctx->hybrid;
}
#endif

static bool pred_cre(DisasContext *ctx)
{
#ifdef TARGET_CHERI
    return ctx->cre;
#else
    return false;
#endif
}

/* Include the auto-generated decoder for 32 bit insn */
#include "decode-insn32.c.inc"

static bool gen_arith_imm_fn(DisasContext *ctx, arg_i *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, target_long))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);

    func(dest, src1, a->imm);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_arith_imm_tl(DisasContext *ctx, arg_i *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = tcg_constant_tl(a->imm);

    func(dest, src1, src2);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static void gen_pack(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_deposit_tl(ret, arg1, arg2,
                       TARGET_LONG_BITS / 2,
                       TARGET_LONG_BITS / 2);
}

static void gen_packu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_shri_tl(t, arg1, TARGET_LONG_BITS / 2);
    tcg_gen_deposit_tl(ret, arg2, t, 0, TARGET_LONG_BITS / 2);
    tcg_temp_free(t);
}

static void gen_packh(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_ext8u_tl(t, arg2);
    tcg_gen_deposit_tl(ret, arg1, t, 8, TARGET_LONG_BITS - 8);
    tcg_temp_free(t);
}

static void gen_sbop_mask(TCGv ret, TCGv shamt)
{
    tcg_gen_movi_tl(ret, 1);
    tcg_gen_shl_tl(ret, ret, shamt);
}

static void gen_bset(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_or_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static void gen_bclr(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_andc_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static void gen_binv(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_xor_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static void gen_bext(TCGv ret, TCGv arg1, TCGv shamt)
{
    tcg_gen_shr_tl(ret, arg1, shamt);
    tcg_gen_andi_tl(ret, ret, 1);
}

static void gen_slo(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_not_tl(ret, arg1);
    tcg_gen_shl_tl(ret, ret, arg2);
    tcg_gen_not_tl(ret, ret);
}

static void gen_sro(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_not_tl(ret, arg1);
    tcg_gen_shr_tl(ret, ret, arg2);
    tcg_gen_not_tl(ret, ret);
}

static bool gen_grevi(DisasContext *ctx, arg_grevi *a)
{
    TCGv source1 = tcg_temp_new();
    TCGv source2;

    gen_get_gpr(ctx, source1, a->rs1);

    if (a->shamt == (TARGET_LONG_BITS - 8)) {
        /* rev8, byte swaps */
        tcg_gen_bswap_tl(source1, source1);
    } else {
        source2 = tcg_temp_new();
        tcg_gen_movi_tl(source2, a->shamt);
        gen_helper_grev(source1, source1, source2);
        tcg_temp_free(source2);
    }

    gen_set_gpr(ctx, a->rd, source1);
    tcg_temp_free(source1);
    return true;
}

#define GEN_SHADD(SHAMT)                                       \
static void gen_sh##SHAMT##add(TCGv ret, TCGv arg1, TCGv arg2) \
{                                                              \
    TCGv t = tcg_temp_new();                                   \
                                                               \
    tcg_gen_shli_tl(t, arg1, SHAMT);                           \
    tcg_gen_add_tl(ret, t, arg2);                              \
                                                               \
    tcg_temp_free(t);                                          \
}

GEN_SHADD(1)
GEN_SHADD(2)
GEN_SHADD(3)

static void gen_ctzw(TCGv ret, TCGv arg1)
{
    tcg_gen_ori_tl(ret, arg1, (target_ulong)MAKE_64BIT_MASK(32, 32));
    tcg_gen_ctzi_tl(ret, ret, 64);
}

static void gen_clzw(TCGv ret, TCGv arg1)
{
    tcg_gen_ext32u_tl(ret, arg1);
    tcg_gen_clzi_tl(ret, ret, 64);
    tcg_gen_subi_tl(ret, ret, 32);
}

static void gen_cpopw(TCGv ret, TCGv arg1)
{
    tcg_gen_ext32u_tl(arg1, arg1);
    tcg_gen_ctpop_tl(ret, arg1);
}

static void gen_packw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_ext16s_tl(t, arg2);
    tcg_gen_deposit_tl(ret, arg1, t, 16, 48);
    tcg_temp_free(t);
}

static void gen_packuw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_shri_tl(t, arg1, 16);
    tcg_gen_deposit_tl(ret, arg2, t, 0, 16);
    tcg_gen_ext32s_tl(ret, ret);
    tcg_temp_free(t);
}

static void gen_rorw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    /* truncate to 32-bits */
    tcg_gen_trunc_tl_i32(t1, arg1);
    tcg_gen_trunc_tl_i32(t2, arg2);

    tcg_gen_rotr_i32(t1, t1, t2);

    /* sign-extend 64-bits */
    tcg_gen_ext_i32_tl(ret, t1);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static void gen_rolw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    /* truncate to 32-bits */
    tcg_gen_trunc_tl_i32(t1, arg1);
    tcg_gen_trunc_tl_i32(t2, arg2);

    tcg_gen_rotl_i32(t1, t1, t2);

    /* sign-extend 64-bits */
    tcg_gen_ext_i32_tl(ret, t1);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static void gen_grevw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_ext32u_tl(arg1, arg1);
    gen_helper_grev(ret, arg1, arg2);
}

static void gen_gorcw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_ext32u_tl(arg1, arg1);
    gen_helper_gorcw(ret, arg1, arg2);
}

#define GEN_SHADD_UW(SHAMT)                                       \
static void gen_sh##SHAMT##add_uw(TCGv ret, TCGv arg1, TCGv arg2) \
{                                                                 \
    TCGv t = tcg_temp_new();                                      \
                                                                  \
    tcg_gen_ext32u_tl(t, arg1);                                   \
                                                                  \
    tcg_gen_shli_tl(t, t, SHAMT);                                 \
    tcg_gen_add_tl(ret, t, arg2);                                 \
                                                                  \
    tcg_temp_free(t);                                             \
}

GEN_SHADD_UW(1)
GEN_SHADD_UW(2)
GEN_SHADD_UW(3)

static void gen_add_uw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_ext32u_tl(arg1, arg1);
    tcg_gen_add_tl(ret, arg1, arg2);
}

static bool gen_arith(DisasContext *ctx, arg_r *a, DisasExtend ext,
                      void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = get_gpr(ctx, a->rs2, ext);

    func(dest, src1, src2);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_shift(DisasContext *ctx, arg_r *a,
                        void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();

    gen_get_gpr(ctx, source1, a->rs1);
    gen_get_gpr(ctx, source2, a->rs2);

    tcg_gen_andi_tl(source2, source2, TARGET_LONG_BITS - 1);
    (*func)(source1, source1, source2);

    gen_set_gpr(ctx, a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static uint32_t opcode_at(DisasContextBase *dcbase, target_ulong pc)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUState *cpu = ctx->cs;
    CPURISCVState *env = cpu->env_ptr;

    return cpu_ldl_code(env, pc);
}

static bool gen_shifti(DisasContext *ctx, arg_shift *a,
                       void(*func)(TCGv, TCGv, TCGv))
{
    if (a->shamt >= TARGET_LONG_BITS) {
        return false;
    }

    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();

    gen_get_gpr(ctx, source1, a->rs1);

    tcg_gen_movi_tl(source2, a->shamt);
    (*func)(source1, source1, source2);

    gen_set_gpr(ctx, a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static bool gen_shiftw(DisasContext *ctx, arg_r *a,
                       void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();

    gen_get_gpr(ctx, source1, a->rs1);
    gen_get_gpr(ctx, source2, a->rs2);

    tcg_gen_andi_tl(source2, source2, 31);
    (*func)(source1, source1, source2);
    tcg_gen_ext32s_tl(source1, source1);

    gen_set_gpr(ctx, a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static bool gen_shiftiw(DisasContext *ctx, arg_shift *a,
                        void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();

    gen_get_gpr(ctx, source1, a->rs1);
    tcg_gen_movi_tl(source2, a->shamt);

    (*func)(source1, source1, source2);
    tcg_gen_ext32s_tl(source1, source1);

    gen_set_gpr(ctx, a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static void gen_ctz(TCGv ret, TCGv arg1)
{
    tcg_gen_ctzi_tl(ret, arg1, TARGET_LONG_BITS);
}

static void gen_clz(TCGv ret, TCGv arg1)
{
    tcg_gen_clzi_tl(ret, arg1, TARGET_LONG_BITS);
}

static bool gen_unary(DisasContext *ctx, arg_r2 *a,
                      void(*func)(TCGv, TCGv))
{
    TCGv source = tcg_temp_new();

    gen_get_gpr(ctx, source, a->rs1);

    (*func)(source, source);

    gen_set_gpr(ctx, a->rd, source);
    tcg_temp_free(source);
    return true;
}

/* Include insn module translation function */
#ifdef TARGET_CHERI
/* Must be included first since the helpers are used by trans_rvi.c.inc */
#include "insn_trans/trans_cheri.c.inc"
#endif

// Helpers to generate a virtual address that has been checked by the CHERI
// capability helpers: If ctx->capmode is set, the register number will be
// a capability and we check that capability, otherwise we treat the register
// as an offset relative to $ddc and check if that is in bounds.
// Note: the return value must be freed with tcg_temp_free_cap_checked()
static inline TCGv_cap_checked_ptr _get_capmode_dependent_addr(
    DisasContext *ctx, int reg_num, target_long regoffs,
#ifdef TARGET_CHERI
    void (*gen_check_cap)(TCGv_cap_checked_ptr, uint32_t, target_long, MemOp),
    void (*check_ddc)(TCGv_cap_checked_ptr, DisasContext *, TCGv, target_ulong),
#endif
    MemOp mop)

{
    TCGv_cap_checked_ptr result = tcg_temp_new_cap_checked();
#ifdef TARGET_CHERI
    if (ctx->capmode) {
        gen_check_cap(result, reg_num, regoffs, mop);
    } else {
        generate_get_ddc_checked_gpr_plus_offset(result, ctx, reg_num, regoffs,
                                                 mop, check_ddc);
    }
#else
    gen_get_gpr(ctx, result, reg_num);
    if (!__builtin_constant_p(regoffs) || regoffs != 0) {
        tcg_gen_addi_tl(result, result, regoffs);
    }
#endif
    return result;
}

static inline TCGv_cap_checked_ptr
get_capmode_dependent_load_addr(DisasContext *ctx, int reg_num,
                               target_long regoffs, MemOp mop)
{
    return _get_capmode_dependent_addr(ctx, reg_num, regoffs,
#ifdef TARGET_CHERI
                                       &generate_cap_load_check_imm,
                                       &generate_ddc_checked_load_ptr,
#endif
                                       mop);
}

static inline TCGv_cap_checked_ptr
get_capmode_dependent_store_addr(DisasContext *ctx, int reg_num,
                                target_long regoffs, MemOp mop)
{
    return _get_capmode_dependent_addr(ctx, reg_num, regoffs,
#ifdef TARGET_CHERI
                                       &generate_cap_store_check_imm,
                                       &generate_ddc_checked_store_ptr,
#endif
                                       mop);
}

static inline TCGv_cap_checked_ptr
get_capmode_dependent_rmw_addr(DisasContext *ctx, int reg_num,
                               target_long regoffs, MemOp mop)
{
    return _get_capmode_dependent_addr(ctx, reg_num, regoffs,
#ifdef TARGET_CHERI
                                       &generate_cap_rmw_check_imm,
                                       &generate_ddc_checked_rmw_ptr,
#endif
                                       mop);
}

#include "insn_trans/trans_rvi.c.inc"
#include "insn_trans/trans_rvm.c.inc"
#include "insn_trans/trans_rva.c.inc"
#include "insn_trans/trans_rvf.c.inc"
#include "insn_trans/trans_rvd.c.inc"
#include "insn_trans/trans_rvh.c.inc"
#include "insn_trans/trans_rvv.c.inc"
#include "insn_trans/trans_rvb.c.inc"
#include "insn_trans/trans_privileged.c.inc"

/* Include the auto-generated decoder for 16 bit insn */
#include "decode-insn16.c.inc"

static bool trans_c_hint(DisasContext *ctx, arg_c_hint *a)
{
    return true;
}

#ifndef TARGET_CHERI

#define TRANS_STUB(instr) \
static bool trans_ ## instr(DisasContext *ctx, arg_ ## instr *a) \
{ \
    g_assert_not_reached(); \
    return false; \
}

/* Stubs needed for mode-dependent compressed instructions */
TRANS_STUB(lc)
TRANS_STUB(sc)
TRANS_STUB(caddi)
TRANS_STUB(cadd)
TRANS_STUB(lr_c)
TRANS_STUB(sc_c)
TRANS_STUB(amoswap_c)
TRANS_STUB(scbndsi)
#endif

static void decode_opc(CPURISCVState *env, DisasContext *ctx)
{
#ifdef CONFIG_RVFI_DII
    // We have to avoid memory accesses for injected instructions since
    // the PC could point somewhere invalid.
    uint16_t opcode = env->rvfi_dii_have_injected_insn
                          ? env->rvfi_dii_injected_insn
                          : translator_lduw(env, ctx->base.pc_next);
    gen_rvfi_dii_set_field_const_i64(PC, pc_rdata, ctx->base.pc_next);
#else
    uint16_t opcode = translator_lduw(env, ctx->base.pc_next);
#endif
    /* check for compressed insn */
    if (extract16(opcode, 0, 2) != 3) {
        gen_riscv_log_instr16(ctx, opcode);
        gen_check_pcc_bounds_next_inst(ctx, 2);
        gen_rvfi_dii_set_field_const_i64(INST, insn, opcode);
        if (!has_ext(ctx, RVC)) {
            gen_exception_illegal(ctx);
        } else {
            ctx->pc_succ_insn = ctx->base.pc_next + 2;
            if (!decode_insn16(ctx, opcode)) {
                gen_exception_illegal(ctx);
            }
        }
    } else {
#ifdef CONFIG_RVFI_DII
        // We have to avoid memory accesses for injected instructions since
        // the PC could point somewhere invalid.
        uint16_t next_16 = env->rvfi_dii_have_injected_insn
                          ? (env->rvfi_dii_injected_insn >> 16)
                          : translator_lduw(env, ctx->base.pc_next + 2);
#else
        uint16_t next_16 = translator_lduw(env, ctx->base.pc_next + 2);
#endif
        uint32_t opcode32 = opcode;
        opcode32 = deposit32(opcode32, 16, 16, next_16);
        gen_riscv_log_instr32(ctx, opcode32);
        gen_check_pcc_bounds_next_inst(ctx, 4);
        ctx->pc_succ_insn = ctx->base.pc_next + 4;
        gen_rvfi_dii_set_field_const_i64(INST, insn, opcode32);
        if (!decode_insn32(ctx, opcode32)) {
            gen_exception_illegal(ctx);
        }
    }
}

static void riscv_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cs->env_ptr;
    RISCVCPU *cpu = RISCV_CPU(cs);
    uint32_t tb_flags = ctx->base.tb->flags;

    ctx->pc_succ_insn = ctx->base.pc_first;
    ctx->mem_idx = tb_flags & TB_FLAGS_MMU_MASK;
    ctx->mstatus_fs = tb_flags & TB_FLAGS_MSTATUS_FS;
#ifdef TARGET_CHERI
    ctx->capmode = tb_in_capmode(ctx->base.tb);
    ctx->hybrid = riscv_feature(env, RISCV_FEATURE_CHERI_HYBRID);
    ctx->cre = riscv_cpu_mode_cre(env);
#endif
    ctx->priv_ver = env->priv_ver;
#if !defined(CONFIG_USER_ONLY)
    if (riscv_has_ext(env, RVH)) {
        ctx->virt_enabled = riscv_cpu_virt_enabled(env);
    } else {
        ctx->virt_enabled = false;
    }
#else
    ctx->virt_enabled = false;
#endif
    ctx->misa = env->misa;
    ctx->frm = -1;  /* unknown rounding mode */
    ctx->ext_ifencei = cpu->cfg.ext_ifencei;
    ctx->vlen = cpu->cfg.vlen;
    ctx->hlsx = FIELD_EX32(tb_flags, TB_FLAGS, HLSX);
    ctx->vill = FIELD_EX32(tb_flags, TB_FLAGS, VILL);
    ctx->sew = FIELD_EX32(tb_flags, TB_FLAGS, SEW);
    ctx->lmul = FIELD_EX32(tb_flags, TB_FLAGS, LMUL);
    ctx->mlen = 1 << (ctx->sew  + 3 - ctx->lmul);
    ctx->vl_eq_vlmax = FIELD_EX32(tb_flags, TB_FLAGS, VL_EQ_VLMAX);
    ctx->cs = cs;
    ctx->w = false;
    ctx->ntemp = 0;
    memset(ctx->temp, 0, sizeof(ctx->temp));

    ctx->zero = tcg_constant_tl(0);
}

static void riscv_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void riscv_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static void riscv_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cpu->env_ptr;

    decode_opc(env, ctx);
    ctx->base.pc_next = ctx->pc_succ_insn;
    ctx->w = false;

    for (int i = ctx->ntemp - 1; i >= 0; --i) {
        tcg_temp_free(ctx->temp[i]);
        ctx->temp[i] = NULL;
    }
    ctx->ntemp = 0;

    gen_rvfi_dii_set_field_const_i64(PC, pc_wdata, ctx->base.pc_next);

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_start;

        page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
        if (ctx->base.pc_next - page_start >= TARGET_PAGE_SIZE) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void riscv_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_TOO_MANY:
        /* CHERI PCC bounds check done on next ifetch. */
        gen_goto_tb(ctx, 0, ctx->base.pc_next, /*bounds_check=*/false);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void riscv_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
#ifndef CONFIG_USER_ONLY
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
#endif

#ifdef CONFIG_RVFI_DII
    if (env->rvfi_dii_have_injected_insn) {
        assert(dcbase->num_insns == 1);
        FILE *logfile = qemu_log_lock();
        uint32_t insn = env->rvfi_dii_injected_insn;
        if (logfile) {
            fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
            target_disas_buf(stderr, cpu, &insn, sizeof(insn), dcbase->pc_first, 1);
        }
        qemu_log_unlock(logfile);
    }
#else
    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
#ifndef CONFIG_USER_ONLY
    qemu_log("Priv: "TARGET_FMT_ld"; Virt: "TARGET_FMT_ld"\n", env->priv, env->virt);
#endif
    log_target_disas(cpu, dcbase->pc_first, dcbase->tb->size);
#endif
}

static const TranslatorOps riscv_tr_ops = {
    .init_disas_context = riscv_tr_init_disas_context,
    .tb_start           = riscv_tr_tb_start,
    .insn_start         = riscv_tr_insn_start,
    .translate_insn     = riscv_tr_translate_insn,
    .tb_stop            = riscv_tr_tb_stop,
    .disas_log          = riscv_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext ctx;

    translator_loop(&riscv_tr_ops, &ctx.base, cs, tb, max_insns);
}

void riscv_translate_init(void)
{
    int i;


    /* cpu_gpr[0] is a placeholder for the zero register. Do not use it. */
    /* Use the gen_set_gpr and gen_get_gpr helper functions when accessing */
    /* registers, unless you specifically block reads/writes to reg 0 */
#ifndef TARGET_CHERI
    cpu_gpr[0] = NULL;
    for (i = 1; i < 32; i++) {
        cpu_gpr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPURISCVState, gpr[i]), riscv_int_regnames[i]);
    }
#else
    /* CNULL cursor should never be written! */
    _cpu_cursors_do_not_access_directly[0] = NULL;
    /*
     * Provide fast access to integer part of capability registers using
     * gen_get_gpr() and get_set_gpr(). But don't expose the cpu_gprs TCGv
     * directly to avoid errors.
     */
    for (i = 1; i < 32; i++) {
        _cpu_cursors_do_not_access_directly[i] = tcg_global_mem_new(
            cpu_env,
            offsetof(CPURISCVState, gpcapregs.decompressed[i].cap._cr_cursor),
            riscv_int_regnames[i]);
    }
#endif
#ifdef CONFIG_RVFI_DII
    cpu_rvfi_available_fields = tcg_global_mem_new_i32(
        cpu_env, offsetof(CPURISCVState, rvfi_dii_trace.available_fields),
        "rvfi_available_fields");
#endif

    for (i = 0; i < 32; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(cpu_env,
            offsetof(CPURISCVState, fpr[i]), riscv_fpr_regnames[i]);
    }

#ifdef TARGET_CHERI
    cpu_pc = tcg_global_mem_new(cpu_env,
                                offsetof(CPURISCVState, PCC._cr_cursor), "pc");
    /// XXXAR: We currently interpose using DDC.cursor and not DDC.base!
    ddc_interposition = tcg_global_mem_new(
        cpu_env, offsetof(CPURISCVState, DDC._cr_cursor), "ddc_interpose");
#else
    cpu_pc = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, pc), "pc");
#endif
    cpu_vl = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, vl), "vl");
#ifdef CONFIG_DEBUG_TCG
    _pc_is_current = tcg_global_mem_new(
        cpu_env, offsetof(CPURISCVState, _pc_is_current), "_pc_is_current");
#endif
    load_res = (TCGv_cap_checked_ptr)tcg_global_mem_new(
        cpu_env, offsetof(CPURISCVState, load_res), "load_res");
    load_val = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_val),
                             "load_val");
}

void gen_cheri_break_loadlink(TCGv_cap_checked_ptr out_addr)
{
    // The SC implementation uses load_res directly, and apparently this helper
    // can be called from inside the addr==load_res check and the cmpxchg being
    // executed.
    // Until this is fixed, comment out the invalidation
    // tcg_gen_movi_tl((TCGv)load_res, -1);
}

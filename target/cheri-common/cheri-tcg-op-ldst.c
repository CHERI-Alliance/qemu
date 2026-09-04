/*
 * Checked-pointer ("_with_checked_addr") TCG load/store/atomic entry points.
 *
 * tcg/tcg-op.h redirects the standard tcg_gen_qemu_ld/st/atomic/cmpxchg
 * calls to the "_with_checked_addr" functions defined here, for every
 * target, CHERI or not: non-CHERI target code reaches them through that
 * macro redirect, and CHERI target code (where the plain names are
 * poisoned) calls them directly once it has produced a capability-checked
 * address.
 *
 * The load/store/atomic generation itself -- canonicalizing the MemOp,
 * emitting the TCG ldst/atomic op, the software-bswap fallback, the plugin
 * memory callbacks -- is not implemented here: it is delegated to the
 * "_chk" API (tcg_gen_qemu_ld_i32_chk() and friends, declared in
 * tcg/tcg-op-common.h) implemented once in tcg/tcg-op-ldst.c, part of the
 * shared, target-independent tcg_ss library. What this file adds, wrapped
 * around those calls, is the target-specific behavior that rides along with
 * a load/store/atomic op on the targets that have it:
 *
 *  - capability tag invalidation on stores (gen_helper_cheri_invalidate_tags,
 *    gen_helper_cheri_invalidate_tags_condition), active under TARGET_CHERI;
 *  - breaking an outstanding load-linked/store-conditional reservation
 *    (gen_cheri_break_loadlink), active under TARGET_MIPS or TARGET_RISCV;
 *  - per-instruction memory-access logging (gen_helper_qemu_log_instr_load
 *    and _store, both 32- and 64-bit), active under CONFIG_TCG_LOG_INSTR;
 *  - RVFI-DII memory-access recording (gen_rvfi_dii_set_mem_data_i32/i64,
 *    from target/riscv/cpu.h), active under TARGET_RISCV && CONFIG_RVFI_DII.
 *
 * All of the above are target-specific macros/symbols (some are even
 * per-target DEF_HELPER-generated helper prototypes), so they can only be
 * referenced from a translation unit that is compiled with that target's
 * own macros defined -- which is exactly why this file, unlike
 * tcg/tcg-op-ldst.c itself, is *not* part of the shared tcg_ss library, and
 * why (unlike the rest of target/cheri-common/, which is only compiled for
 * TARGET_CHERI) it is compiled for every target: the redirect in tcg-op.h
 * applies to every target, not just CHERI ones. On targets that define none
 * of TARGET_CHERI / TARGET_MIPS / TARGET_RISCV / CONFIG_TCG_LOG_INSTR /
 * CONFIG_RVFI_DII, every hook below compiles away to nothing and the
 * load/store/cmpxchg entry points reduce to genuinely thin wrappers around
 * the "_chk" calls.
 *
 * The parallel-dispatch ("CF_PARALLEL") 64-bit atomic RMW path
 * (do_atomic_op_i64(), and do_atomic_op_i32() for its own 32-bit-operand
 * fallback) is the one piece that still duplicates the shared
 * implementation's table-dispatch logic locally rather than delegating to
 * it -- see the comment on do_atomic_op_i64() for why.
 *
 * This file also calls directly into a subset of the generic
 * load/store/canonicalization plumbing that tcg/tcg-op-ldst.c exports via
 * tcg/tcg-op-ldst-internal.h -- tcg_canonicalize_memop() (to recompute hook
 * arguments after a "_chk" call), maybe_extend_addr64()/maybe_free_addr64()
 * and tcg_gen_ext_i32()/tcg_gen_ext_i64() (used by the atomic-RMW and
 * cmpxchg paths below that still generate their own TCG ops). The rest of
 * that header's plumbing (gen_ldst, gen_ldst_i64, tcg_gen_req_mo,
 * plugin_maybe_preserve_addr, plugin_gen_mem_callbacks, table_cmpxchg) is
 * reached only transitively, inside the "_chk" calls.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-mo.h"
#include "exec/translation-block.h"
#include "exec/plugin-gen.h"
#include "exec/log_instr.h"
#include "cheri_defs.h"
#include "tcg/tcg-internal.h"
#include "tcg/tcg-op-ldst-internal.h"


#define tcg_ctx_logging_enabled (unlikely(tcg_ctx->gen_tb->cflags & CF_LOG_INSTR))

#if defined(TARGET_RISCV) && defined(CONFIG_RVFI_DII)
static inline uint64_t memop_rvfi_mask(MemOp op) {
    return MAKE_64BIT_MASK(0, memop_size(op));
}
#else
#define gen_rvfi_dii_set_mem_data_i32(rw, addr, val, memop) ((void)0)
#define gen_rvfi_dii_set_mem_data_i64(rw, addr, val, memop) ((void)0)
#endif

void tcg_gen_qemu_ld_i32_with_checked_addr(TCGv_i32 val, TCGv_cap_checked_ptr addr, TCGArg idx, MemOp memop)
{
    TCGTemp *addr_temp = tcgv_cap_checked_ptr_temp(addr);

#if defined(CONFIG_TCG_LOG_INSTR)
    TCGv_cap_checked_ptr saved_load_addr;
    // If addr and val are the same, we need to allocate a temporary
    if ((TCGv)addr == (TCGv)val) {
        saved_load_addr = tcg_temp_new_cap_checked();
        tcg_gen_mov_cap_checked(saved_load_addr, addr);
    } else {
        saved_load_addr = addr;
    }
#endif

    tcg_gen_qemu_ld_i32_chk(val, addr_temp, idx, memop, tcg_ctx->addr_type);

    /*
     * tcg_gen_qemu_ld_i32_chk() leaves the fully bswap-corrected,
     * sign-extended guest-logical value in val once it returns, so the
     * hooks below -- which record what was logically loaded, and the
     * canonical MemOp describing that logical access, rather than a
     * host-byte-order implementation detail of how the load was emitted --
     * read val here, after the call, rather than being spliced into the
     * middle of it.
     */
    MemOp canon_memop = tcg_canonicalize_memop(memop, 0, 0);
    gen_rvfi_dii_set_mem_data_i32(r, addr, val, canon_memop);
#if defined(CONFIG_TCG_LOG_INSTR)
    if (tcg_ctx_logging_enabled) {
        TCGv_i32 tcoi = tcg_constant_i32(make_memop_idx(canon_memop, idx));
        gen_helper_qemu_log_instr_load32(cpu_env, saved_load_addr, val, tcoi);
    }
#endif
}

void handle_conditional_invalidate(TCGv_cap_checked_ptr checked_addr,
                                   MemOp memop, TCGArg mmu_idx,
                                   TCGv_i32 store_happens)
{
#if defined(TARGET_CHERI)
    TCGv_i32 oi = tcg_constant_i32(make_memop_idx(memop, mmu_idx));
    /* Condition is handled in helper */
    gen_helper_cheri_invalidate_tags_condition(cpu_env, checked_addr, oi,
                                               store_happens);
#endif
#if defined(TARGET_MIPS) || defined(TARGET_RISCV)
    gen_cheri_break_loadlink(checked_addr);
#endif
}

static void tcg_gen_qemu_st_i32_with_checked_addr_cond_invalidate(
    TCGv_i32 val, TCGv_cap_checked_ptr addr, TCGArg idx, MemOp memop,
    bool invalidate)
{
    TCGTemp *addr_temp = tcgv_cap_checked_ptr_temp(addr);

    tcg_gen_qemu_st_i32_chk(val, addr_temp, idx, memop, tcg_ctx->addr_type);

    /*
     * val here is the guest-logical value in guest byte order, as passed in
     * by the caller: on a host with no native memory-bswap store op (e.g.
     * aarch64), tcg_gen_qemu_st_i32_chk() byte-swaps into a separate,
     * internal temp before emitting the actual store, and that temp is not
     * visible here. The hooks below therefore record what was logically
     * stored, in guest byte order, rather than the host-byte-order pattern
     * that ends up written to memory on such hosts.
     */
    memop = tcg_canonicalize_memop(memop, 0, 1);
    gen_rvfi_dii_set_mem_data_i32(w, addr, val, memop);
#if defined(TARGET_CHERI) || defined(CONFIG_TCG_LOG_INSTR)
    TCGv_i32 tcoi = tcg_constant_i32(make_memop_idx(memop, idx));
#if defined(CONFIG_TCG_LOG_INSTR)
    if (tcg_ctx_logging_enabled) {
        gen_helper_qemu_log_instr_store32(cpu_env, addr, val, tcoi);
    }
#endif
#if defined(TARGET_CHERI)
    if (invalidate) {
        gen_helper_cheri_invalidate_tags(cpu_env, addr, tcoi);
    }
#endif
#endif
#if defined(TARGET_MIPS) || defined(TARGET_RISCV)
    if (invalidate) {
        gen_cheri_break_loadlink(addr);
    }
#endif
}

void tcg_gen_qemu_st_i32_with_checked_addr(TCGv_i32 val,
                                           TCGv_cap_checked_ptr addr,
                                           TCGArg idx, MemOp memop)
{
    tcg_gen_qemu_st_i32_with_checked_addr_cond_invalidate(val, addr, idx, memop,
                                                          true);
}

void tcg_gen_qemu_ld_i64_with_checked_addr(TCGv_i64 val, TCGv_cap_checked_ptr addr, TCGArg idx, MemOp memop)
{
    TCGTemp *addr_temp = tcgv_cap_checked_ptr_temp(addr);

    if (TCG_TARGET_REG_BITS == 32 && (memop & MO_SIZE) < MO_64) {
        tcg_gen_qemu_ld_i32_with_checked_addr(TCGV_LOW(val), addr, idx, memop);
        if (memop & MO_SIGN) {
            tcg_gen_sari_i32(TCGV_HIGH(val), TCGV_LOW(val), 31);
        } else {
            tcg_gen_movi_i32(TCGV_HIGH(val), 0);
        }
        return;
    }

#if defined(CONFIG_TCG_LOG_INSTR)
    TCGv_cap_checked_ptr saved_load_addr;
    // If addr and val are the same, we need to allocate a temporary
    if ((TCGv)addr == (TCGv)val) {
        saved_load_addr = tcg_temp_new_cap_checked();
        tcg_gen_mov_cap_checked(saved_load_addr, addr);
    } else {
        saved_load_addr = addr;
    }
#endif

    tcg_gen_qemu_ld_i64_chk(val, addr_temp, idx, memop, tcg_ctx->addr_type);

    /*
     * See tcg_gen_qemu_ld_i32_with_checked_addr() for why the hooks below
     * run after the load, reading its final, guest-logical val and a
     * matching canonical MemOp, rather than being spliced into the middle
     * of it.
     */
    MemOp canon_memop = tcg_canonicalize_memop(memop, 1, 0);
    gen_rvfi_dii_set_mem_data_i64(r, addr, val, canon_memop);
#if defined(CONFIG_TCG_LOG_INSTR)
    if (tcg_ctx_logging_enabled) {
        TCGv_i32 tcop = tcg_constant_i32(make_memop_idx(canon_memop, idx));
        gen_helper_qemu_log_instr_load64(cpu_env, saved_load_addr, val, tcop);
    }
#endif
}

void tcg_gen_qemu_st_i64_with_checked_addr_cond_invalidate(
    TCGv_i64 val, TCGv_cap_checked_ptr addr, TCGArg idx, MemOp memop,
    bool invalidate)
{
    TCGTemp *addr_temp = tcgv_cap_checked_ptr_temp(addr);

    if (TCG_TARGET_REG_BITS == 32 && (memop & MO_SIZE) < MO_64) {
        tcg_gen_qemu_st_i32_with_checked_addr_cond_invalidate(
            TCGV_LOW(val), addr, idx, memop, invalidate);
        return;
    }

    tcg_gen_qemu_st_i64_chk(val, addr_temp, idx, memop, tcg_ctx->addr_type);

    /*
     * See tcg_gen_qemu_st_i32_with_checked_addr_cond_invalidate() for why
     * val here is the guest-logical value in guest byte order, rather than
     * a possible host-byte-swapped implementation detail internal to
     * tcg_gen_qemu_st_i64_chk().
     */
    memop = tcg_canonicalize_memop(memop, 1, 1);
    gen_rvfi_dii_set_mem_data_i64(w, addr, val, memop);

#if defined(TARGET_CHERI) || defined(CONFIG_TCG_LOG_INSTR)
    TCGv_i32 tcoi = tcg_constant_i32(make_memop_idx(memop, idx));
#if defined(CONFIG_TCG_LOG_INSTR)
    if (tcg_ctx_logging_enabled) {
        gen_helper_qemu_log_instr_store64(cpu_env, addr, val, tcoi);
    }
#endif
#if defined(TARGET_CHERI)
    if (invalidate) {
        gen_helper_cheri_invalidate_tags(cpu_env, addr, tcoi);
    }
#endif
#endif
#if defined(TARGET_MIPS) || defined(TARGET_RISCV)
    if (invalidate) {
        gen_cheri_break_loadlink(addr);
    }
#endif
}

void tcg_gen_qemu_st_i64_with_checked_addr(TCGv_i64 val,
                                           TCGv_cap_checked_ptr addr,
                                           TCGArg idx, MemOp memop)
{
    tcg_gen_qemu_st_i64_with_checked_addr_cond_invalidate(val, addr, idx, memop,
                                                          true);
}

typedef void (*gen_atomic_op_i32)(TCGv_i32, TCGv_env, TCGv_i64,
                                  TCGv_i32, TCGv_i32);
typedef void (*gen_atomic_op_i64)(TCGv_i64, TCGv_env, TCGv_i64,
                                  TCGv_i64, TCGv_i32);

#ifdef CONFIG_ATOMIC64
# define WITH_ATOMIC64(X) X,
#else
# define WITH_ATOMIC64(X)
#endif

void tcg_gen_nonatomic_cmpxchg_i32_with_checked_addr(
    TCGv_i32 retv, TCGv_cap_checked_ptr checked_addr, TCGv_i32 cmpv,
    TCGv_i32 newv, TCGArg idx, MemOp memop)
{
    memop = tcg_canonicalize_memop(memop, 0, 0);
    TCGv_i32 t1 = tcg_temp_ebb_new_i32();
    TCGv_i32 t2 = tcg_temp_ebb_new_i32();

    tcg_gen_ext_i32(t2, cmpv, memop & MO_SIZE);

    tcg_gen_qemu_ld_i32_with_checked_addr(t1, checked_addr, idx, memop & ~MO_SIGN);
    TCGv_i32 equal = NULL;
#ifdef TARGET_CHERI
    equal = tcg_temp_new_i32();
    tcg_gen_setcond_i32(TCG_COND_EQ, equal, t1, t2);
#endif
    handle_conditional_invalidate(checked_addr, memop, idx, equal);
#ifdef TARGET_CHERI
    tcg_temp_free_i32(equal);
#endif
    tcg_gen_movcond_i32(TCG_COND_EQ, t2, t1, t2, newv, t1);
    tcg_gen_qemu_st_i32_with_checked_addr_cond_invalidate(
        t2, checked_addr, idx, memop, false);
    tcg_temp_free_i32(t2);

    if (memop & MO_SIGN) {
        tcg_gen_ext_i32(retv, t1, memop);
    } else {
        tcg_gen_mov_i32(retv, t1);
    }
    tcg_temp_free_i32(t1);
}

void tcg_gen_atomic_cmpxchg_i32_with_checked_addr(
    TCGv_i32 retv, TCGv_cap_checked_ptr checked_addr, TCGv_i32 cmpv,
    TCGv_i32 newv, TCGArg idx, MemOp memop)
{
    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        tcg_gen_nonatomic_cmpxchg_i32_with_checked_addr(retv, checked_addr,
                                                        cmpv, newv, idx, memop);
        return;
    }

    /*
     * The parallel-dispatch cmpxchg below invalidates no CHERI tag and
     * breaks no load-linked reservation on its own (unlike the non-atomic
     * path above, whose load/store sub-steps carry those hooks), so this
     * delegates straight to the shared, generic implementation.
     */
    ASSERT_IF_CHERI();
    tcg_gen_atomic_cmpxchg_i32_chk(retv, tcgv_cap_checked_ptr_temp(checked_addr),
                                   cmpv, newv, idx, memop, tcg_ctx->addr_type);
}

void tcg_gen_nonatomic_cmpxchg_i64_with_checked_addr(
    TCGv_i64 retv, TCGv_cap_checked_ptr checked_addr, TCGv_i64 cmpv,
    TCGv_i64 newv, TCGArg idx, MemOp memop)
{
    memop = tcg_canonicalize_memop(memop, 1, 0);

    TCGv_i64 t1 = tcg_temp_ebb_new_i64();
    TCGv_i64 t2 = tcg_temp_ebb_new_i64();

    tcg_gen_ext_i64(t2, cmpv, memop & MO_SIZE);
    tcg_gen_qemu_ld_i64_with_checked_addr(t1, checked_addr, idx, memop & ~MO_SIGN);
    TCGv_i32 equal = NULL;
#ifdef TARGET_CHERI
    equal = tcg_temp_new_i32();
    TCGv_i64 equal64 = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_EQ, equal64, t1, t2);
    tcg_gen_extrl_i64_i32(equal, equal64);
    tcg_temp_free_i64(equal64);
#endif
    handle_conditional_invalidate(checked_addr, memop, idx, equal);
#ifdef TARGET_CHERI
    tcg_temp_free_i32(equal);
#endif
    tcg_gen_movcond_i64(TCG_COND_EQ, t2, t1, t2, newv, t1);
    tcg_gen_qemu_st_i64_with_checked_addr_cond_invalidate(
        t2, checked_addr, idx, memop, false);
    tcg_temp_free_i64(t2);

    if (memop & MO_SIGN) {
        tcg_gen_ext_i64(retv, t1, memop);
    } else {
        tcg_gen_mov_i64(retv, t1);
    }
    tcg_temp_free_i64(t1);
}

void tcg_gen_atomic_cmpxchg_i64_with_checked_addr(
    TCGv_i64 retv, TCGv_cap_checked_ptr checked_addr, TCGv_i64 cmpv,
    TCGv_i64 newv, TCGArg idx, MemOp memop)
{
    memop = tcg_canonicalize_memop(memop, 1, 0);

    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        tcg_gen_nonatomic_cmpxchg_i64_with_checked_addr(retv,checked_addr,cmpv,newv,idx,memop);
        return;
    }

    /*
     * As with the i32 cmpxchg above, none of the parallel-dispatch paths
     * here -- the direct MO_64 helper call, or the 32-bit-host fallback
     * that splits into two 32-bit cmpxchg operations -- touch a CHERI tag
     * or a load-linked reservation on their own, so this delegates to the
     * shared, generic implementation for all of them.
     */
    ASSERT_IF_CHERI();
    tcg_gen_atomic_cmpxchg_i64_chk(retv, tcgv_cap_checked_ptr_temp(checked_addr),
                                   cmpv, newv, idx, memop, tcg_ctx->addr_type);
}

enum GEN_OP_SIGN {
    GEN_OP_SIGNED,
    GEN_OP_UNSIGNED,
    GEN_OP_NO_SIGN,
};

// The sign of an atomic operation need not match the sign of a memop.
// For example, arm has a fetch minimum signed byte instruction.
// This does NOT sign-extend the value loaded (and so no MO_SIGN), but expects
// the comparison to be signed. AMOMINU.W on RISCV should be doing an unsigned
// min, but WILL sign-extend the value loaded after. Best way of handling this
// is to do an appropriate load for the operation, then extend the result
// afterwards.

static MemOp get_memop_for_operation(MemOp base_memop,
                                     enum GEN_OP_SIGN gen_sign)
{
    if ((base_memop & MO_SIZE) == MO_64 || gen_sign == GEN_OP_UNSIGNED)
        return base_memop & ~MO_SIGN;
    else if (gen_sign == GEN_OP_SIGNED)
        return base_memop | MO_SIGN;
    else
        return base_memop;
}

static void do_nonatomic_op_i32(TCGv_i32 ret, TCGv_cap_checked_ptr checked_addr,
                                TCGv_i32 val, TCGArg idx, MemOp memop,
                                bool new_val,
                                void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32),
                                enum GEN_OP_SIGN gen_sign)
{
    TCGv_i32 t1 = tcg_temp_ebb_new_i32();
    TCGv_i32 t2 = tcg_temp_ebb_new_i32();

    memop = tcg_canonicalize_memop(memop, 0, 0);

    MemOp tempop = get_memop_for_operation(memop, gen_sign);
    tcg_gen_qemu_ld_i32_with_checked_addr(t1, checked_addr, idx, tempop);
    tcg_gen_ext_i32(t2, val, tempop);
    gen(t2, t1, t2);
    // Note: For CHERI tcg_gen_qemu_st_i32 calls gen_cheri_invalidate_tags()
    tcg_gen_qemu_st_i32_with_checked_addr(t2, checked_addr, idx, memop);

    tcg_gen_ext_i32(ret, (new_val ? t2 : t1), memop);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

/*
 * The GEN_ATOMIC_HELPER-generated tcg_gen_atomic_<op>_i32() below delegates
 * its own CF_PARALLEL dispatch to the shared tcg_gen_atomic_<op>_i32_chk()
 * implementation instead of calling this function, so the only remaining
 * caller of do_atomic_op_i32() is do_atomic_op_i64()'s 32-bit-operand
 * fallback below, which needs a table-parameterized dispatch (the table
 * to use is only known at that call site's runtime, not at compile time),
 * so it cannot be replaced the same way.
 */
static void do_atomic_op_i32(TCGv_i32 ret, TCGv_cap_checked_ptr checked_addr,
                             TCGv_i32 val, TCGArg idx, MemOp memop,
                             void *const table[])
{
    ASSERT_IF_CHERI();
    gen_atomic_op_i32 gen;
    TCGv_i64 a64;
    MemOpIdx oi;

    memop = tcg_canonicalize_memop(memop, 0, 0);

    gen = table[memop & (MO_SIZE | MO_BSWAP)];
    tcg_debug_assert(gen != NULL);

    oi = make_memop_idx(memop & ~MO_SIGN, idx);
    a64 = maybe_extend_addr64(tcgv_cap_checked_ptr_temp(checked_addr));
    gen(ret, cpu_env, a64, val, tcg_constant_i32(oi));
    maybe_free_addr64(a64);
#if defined(TARGET_CHERI)
    TCGv_i32 tcoi = tcg_constant_i32(make_memop_idx(memop, idx));
    gen_helper_cheri_invalidate_tags(cpu_env, checked_addr, tcoi);
#endif
#if defined(TARGET_MIPS) || defined(TARGET_RISCV)
    gen_cheri_break_loadlink(checked_addr);
#endif

    if (memop & MO_SIGN) {
        tcg_gen_ext_i32(ret, ret, memop);
    }
}

static void do_nonatomic_op_i64(TCGv_i64 ret, TCGv_cap_checked_ptr checked_addr,
                                TCGv_i64 val, TCGArg idx, MemOp memop,
                                bool new_val,
                                void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64),
                                enum GEN_OP_SIGN gen_sign)
{
    TCGv_i64 t1 = tcg_temp_ebb_new_i64();
    TCGv_i64 t2 = tcg_temp_ebb_new_i64();

    memop = tcg_canonicalize_memop(memop, 1, 0);
    MemOp tempop = get_memop_for_operation(memop, gen_sign);
    tcg_gen_qemu_ld_i64_with_checked_addr(t1, checked_addr, idx, tempop);
    tcg_gen_ext_i64(t2, val, tempop);
    gen(t2, t1, t2);
    // Note: For CHERI tcg_gen_qemu_st_i64 calls gen_cheri_invalidate_tags()
    tcg_gen_qemu_st_i64_with_checked_addr(t2, checked_addr, idx, memop);

    tcg_gen_ext_i64(ret, (new_val ? t2 : t1), memop);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

/*
 * Unlike do_atomic_op_i32() above, this one is still called directly from
 * the GEN_ATOMIC_HELPER-generated tcg_gen_atomic_<op>_i64() below, rather
 * than that function delegating to the shared tcg_gen_atomic_<op>_i64_chk()
 * implementation: the MO_64 branch here returns immediately after its
 * helper call, before ever reaching the tag-invalidate/load-linked-break
 * hooks at the bottom of this function -- only the sub-32-bit fallback
 * branch (which itself calls do_atomic_op_i32(), carrying its own copy of
 * those hooks) falls through to them. Delegating to the shared, hook-free
 * "_chk" implementation and adding hook calls after it returns cannot
 * reproduce this hook placement without either dropping the hooks entirely
 * for 64-bit-sized ops or firing them twice on the 32-bit-fallback path,
 * so the table-dispatch logic stays duplicated here instead.
 */
static void do_atomic_op_i64(TCGv_i64 ret, TCGv_cap_checked_ptr checked_addr,
                             TCGv_i64 val, TCGArg idx, MemOp memop,
                             void *const table[])
{
    ASSERT_IF_CHERI();
    memop = tcg_canonicalize_memop(memop, 1, 0);
    if ((memop & MO_SIZE) == MO_64) {
        gen_atomic_op_i64 gen = table[memop & (MO_SIZE | MO_BSWAP)];

        if (gen) {
            MemOpIdx oi = make_memop_idx(memop & ~MO_SIGN, idx);
            TCGv_i64 a64 = maybe_extend_addr64(tcgv_cap_checked_ptr_temp(checked_addr));
            gen(ret, cpu_env, a64, val, tcg_constant_i32(oi));
            maybe_free_addr64(a64);
            return;
        }

        gen_helper_exit_atomic(cpu_env);
        /* Produce a result, so that we have a well-formed opcode stream
           with respect to uses of the result in the (dead) code following.  */
        tcg_gen_movi_i64(ret, 0);
    } else {
        TCGv_i32 v32 = tcg_temp_ebb_new_i32();
        TCGv_i32 r32 = tcg_temp_ebb_new_i32();

        tcg_gen_extrl_i64_i32(v32, val);
        do_atomic_op_i32(r32, checked_addr, v32, idx, memop & ~MO_SIGN, table);
        tcg_temp_free_i32(v32);

        tcg_gen_extu_i32_i64(ret, r32);
        tcg_temp_free_i32(r32);

        if (memop & MO_SIGN) {
            tcg_gen_ext_i64(ret, ret, memop);
        }
    }
#if defined(TARGET_CHERI)
    TCGv_i32 tcoi = tcg_constant_i32(make_memop_idx(memop, idx));
    gen_helper_cheri_invalidate_tags(cpu_env, checked_addr, tcoi);
#endif
#if defined(TARGET_MIPS) || defined(TARGET_RISCV)
    gen_cheri_break_loadlink(checked_addr);
#endif
}

/*
 * Tag-invalidate / load-linked-break hooks for a parallel-dispatch atomic
 * RMW op whose table-lookup-and-helper-call dispatch has been delegated to
 * the shared tcg_gen_atomic_<op>_i32_chk() implementation in
 * tcg/tcg-op-ldst.c. do_atomic_op_i32() above carries the equivalent hooks
 * for its own caller, which cannot delegate the same way.
 */
static void atomic_rmw_i32_hooks(TCGv_cap_checked_ptr checked_addr,
                                 MemOp canon_memop, TCGArg idx)
{
#if defined(TARGET_CHERI)
    TCGv_i32 tcoi = tcg_constant_i32(make_memop_idx(canon_memop, idx));
    gen_helper_cheri_invalidate_tags(cpu_env, checked_addr, tcoi);
#endif
#if defined(TARGET_MIPS) || defined(TARGET_RISCV)
    gen_cheri_break_loadlink(checked_addr);
#endif
}

#define GEN_ATOMIC_HELPER(NAME, OP, NEW, SIGNED)                        \
static void * const table_##NAME[(MO_SIZE | MO_BSWAP) + 1] = {          \
    [MO_8] = gen_helper_atomic_##NAME##b,                               \
    [MO_16 | MO_LE] = gen_helper_atomic_##NAME##w_le,                   \
    [MO_16 | MO_BE] = gen_helper_atomic_##NAME##w_be,                   \
    [MO_32 | MO_LE] = gen_helper_atomic_##NAME##l_le,                   \
    [MO_32 | MO_BE] = gen_helper_atomic_##NAME##l_be,                   \
    WITH_ATOMIC64([MO_64 | MO_LE] = gen_helper_atomic_##NAME##q_le)     \
    WITH_ATOMIC64([MO_64 | MO_BE] = gen_helper_atomic_##NAME##q_be)     \
};                                                                      \
void tcg_gen_atomic_##NAME##_i32                                        \
    (TCGv_i32 ret, TCGv_cap_checked_ptr addr, TCGv_i32 val, TCGArg idx, MemOp memop)    \
{                                                                       \
    if (tcg_ctx->gen_tb->cflags & CF_PARALLEL) {                        \
        ASSERT_IF_CHERI();                                              \
        MemOp canon_memop = tcg_canonicalize_memop(memop, 0, 0);        \
        tcg_gen_atomic_##NAME##_i32_chk(ret, tcgv_cap_checked_ptr_temp(addr), \
                                        val, idx, memop, tcg_ctx->addr_type); \
        atomic_rmw_i32_hooks(addr, canon_memop, idx);                   \
    } else {                                                            \
        do_nonatomic_op_i32(ret, addr, val, idx, memop, NEW,            \
                            tcg_gen_##OP##_i32, SIGNED);                \
    }                                                                   \
}                                                                       \
void tcg_gen_atomic_##NAME##_i64                                        \
    (TCGv_i64 ret, TCGv_cap_checked_ptr addr, TCGv_i64 val, TCGArg idx, MemOp memop)    \
{                                                                       \
    if (tcg_ctx->gen_tb->cflags & CF_PARALLEL) {                        \
        do_atomic_op_i64(ret, addr, val, idx, memop, table_##NAME);     \
    } else {                                                            \
        do_nonatomic_op_i64(ret, addr, val, idx, memop, NEW,            \
                            tcg_gen_##OP##_i64, SIGNED);                \
    }                                                                   \
}

GEN_ATOMIC_HELPER(fetch_add, add, 0, GEN_OP_NO_SIGN)
GEN_ATOMIC_HELPER(fetch_and, and, 0, GEN_OP_NO_SIGN)
GEN_ATOMIC_HELPER(fetch_or, or, 0, GEN_OP_NO_SIGN)
GEN_ATOMIC_HELPER(fetch_xor, xor, 0, GEN_OP_NO_SIGN)
GEN_ATOMIC_HELPER(fetch_smin, smin, 0, GEN_OP_SIGNED)
GEN_ATOMIC_HELPER(fetch_umin, umin, 0, GEN_OP_UNSIGNED)
GEN_ATOMIC_HELPER(fetch_smax, smax, 0, GEN_OP_SIGNED)
GEN_ATOMIC_HELPER(fetch_umax, umax, 0, GEN_OP_UNSIGNED)

GEN_ATOMIC_HELPER(add_fetch, add, 1, GEN_OP_NO_SIGN)
GEN_ATOMIC_HELPER(and_fetch, and, 1, GEN_OP_NO_SIGN)
GEN_ATOMIC_HELPER(or_fetch, or, 1, GEN_OP_NO_SIGN)
GEN_ATOMIC_HELPER(xor_fetch, xor, 1, GEN_OP_NO_SIGN)
GEN_ATOMIC_HELPER(smin_fetch, smin, 1, GEN_OP_SIGNED)
GEN_ATOMIC_HELPER(umin_fetch, umin, 1, GEN_OP_UNSIGNED)
GEN_ATOMIC_HELPER(smax_fetch, smax, 1, GEN_OP_SIGNED)
GEN_ATOMIC_HELPER(umax_fetch, umax, 1, GEN_OP_UNSIGNED)

static void tcg_gen_mov2_i32(TCGv_i32 r, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_mov_i32(r, b);
}

static void tcg_gen_mov2_i64(TCGv_i64 r, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_mov_i64(r, b);
}

GEN_ATOMIC_HELPER(xchg, mov2, 0, GEN_OP_NO_SIGN)

#undef GEN_ATOMIC_HELPER

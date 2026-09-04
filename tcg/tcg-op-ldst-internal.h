/* SPDX-License-Identifier: MIT */
/*
 * Internal declarations shared between tcg/tcg-op-ldst.c (the generic,
 * target-independent load/store/atomic implementation, compiled once and
 * shared across every target) and target/cheri-common/cheri-tcg-op-ldst.c
 * (the "_with_checked_addr" / checked-pointer entry points that every
 * target's tcg-op.h redirects standard load/store/atomic calls to, compiled
 * once per target because it also generates the CHERI capability-tag
 * invalidation, load-linked/store-conditional breaking, and instruction
 * logging hooks that ride along with those operations on the targets that
 * have them).
 *
 * Everything declared here is plumbing private to those two translation
 * units; it is not part of the public TCG code-generation API, which is why
 * it lives in this internal header rather than tcg/tcg-op-common.h.
 */

#ifndef TCG_TCG_OP_LDST_INTERNAL_H
#define TCG_TCG_OP_LDST_INTERNAL_H

MemOp tcg_canonicalize_memop(MemOp op, bool is64, bool st);

void gen_ldst(TCGOpcode opc, TCGTemp *vl, TCGTemp *vh,
              TCGTemp *addr, MemOpIdx oi);
void gen_ldst_i64(TCGOpcode opc, TCGv_i64 v, TCGTemp *addr, MemOpIdx oi);

void tcg_gen_req_mo(TCGBar type);

TCGv_i64 plugin_maybe_preserve_addr(TCGTemp *addr);
void plugin_gen_mem_callbacks(TCGv_i64 copy_addr, TCGTemp *orig_addr,
                               MemOpIdx oi, enum qemu_plugin_mem_rw rw);

TCGv_i64 maybe_extend_addr64(TCGTemp *addr);
void maybe_free_addr64(TCGv_i64 a64);

void tcg_gen_ext_i32(TCGv_i32 ret, TCGv_i32 val, MemOp opc);
void tcg_gen_ext_i64(TCGv_i64 ret, TCGv_i64 val, MemOp opc);

/*
 * Helper-function table for the various cmpxchg opcodes, indexed by
 * (MemOp & (MO_SIZE | MO_BSWAP)). Used by both the generic i128 cmpxchg
 * path in tcg-op-ldst.c and the checked-pointer i32/i64 cmpxchg path in
 * cheri-tcg-op-ldst.c.
 */
extern void * const table_cmpxchg[(MO_SIZE | MO_BSWAP) + 1];

#endif /* TCG_TCG_OP_LDST_INTERNAL_H */

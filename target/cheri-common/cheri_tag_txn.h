/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) Codasip 2026
 *
 * This work was supported by Innovate UK project 105694, "Digital Security
 * by Design (DSbD) Technology Platform Prototype".
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
#ifndef CHERI_TAG_TXN_H
#define CHERI_TAG_TXN_H

#include "target/cheri-common/cheri_tagmem.h"
#include "target/cheri-common/cheri_tag_txn_types.h"

#if defined(TARGET_CHERI)

CheriTagBlock *cheri_tag_block_dummy(target_ulong vaddr);
void cheri_tag_block_clear_range(CheriTagBlock *tagblk, size_t cap_idx,
                                 size_t end_idx);
void cheri_tag_txn_log_tags(CheriTagTxn *txn);
void cheri_tag_txn_log_tag_write(CheriTagTxn *txn);

/* API functions for Cheri Tag Transaction Locking */

/* Cheri tag transaction API cpu registration functions */

/*
 * In the event of a guest fault between lock and commit, QEMU jumps to the
 * CPU loop, and transactions in the stack are gone. So we keep the record with
 * the CPU so that cheri_tag_txn_exception_thrown has a record of the
 * transactions.
 */

static inline QEMU_ALWAYS_INLINE void
cheri_tag_txn_cpu_register(CheriTagTxn *txn)
{
    int i;
    CheriTagTxnCpu *cpu = &txn->cpu->cheri_txn;

    cheri_debug_assert(cpu->nheld + txn->nlocks <= CHERI_TAG_TXN_MAX_HELD);
    txn->outer = cpu->active;
    txn->held_base = cpu->nheld;

    for (i = 0; i < txn->nlocks; i++) {
        cpu->held[cpu->nheld + i] = txn->lock[i];
    }
    cpu->nheld += txn->nlocks;
    cpu->active = txn;
}

static inline QEMU_ALWAYS_INLINE void
cheri_tag_txn_cpu_unregister(CheriTagTxn *txn)
{
    CheriTagTxnCpu *cpu = &txn->cpu->cheri_txn;

    cheri_debug_assert(cpu->active == txn);
    cpu->nheld = txn->held_base;
    cpu->active = txn->outer;
}

/* Initialise the Transaction with CPU state and Flags given */
static inline QEMU_ALWAYS_INLINE void
cheri_tag_txn_init(CheriTagTxn *txn, CPUState *cpu, uint8_t flags)
{
    CheriTagTxn newtxn = { 0 };
    newtxn.cpu = cpu;
    newtxn.flags = flags;
    *txn = newtxn;
}

/*
 * Add a block to the locks. Must maintain the order REAL->Dummy in ascending
 * address order.
 */
static inline QEMU_ALWAYS_INLINE void
cheri_tag_txn_add_lock(CheriTagTxn *txn, CheriTagBlock *block, bool dummy)
{
    size_t i;
    void *tmp;
    for (i = 0; i < txn->nlocks; i++) {
        if (block == txn->lock[i]) {
            /* We already have this lock so can just return */
            return;
        }
    }
    cheri_debug_assert(txn->nlocks < CHERI_TAG_TXN_MAX);
    /*
     * Add the block to the list and can be at most 2 entries.
     * The ordering rules are :-
     * Real locks first, in ascending order.
     * Then dummy locks (also in ascending order).
     */
    if (txn->nlocks == 0) {
        /* Easy case no other locks, so no swapping shenanigans needed */
        txn->lock[0] = block;
    } else if ((dummy && (txn->nreal == 0)) || (!dummy && (txn->nreal == 1))) {
        /*
         * Either adding a dummy lock to a dummy lock  or a real lock to a real
         * lock. In either case we sort by acending order.
         */
        tmp = txn->lock[0];
        txn->lock[0] = MIN(tmp, block);
        txn->lock[1] = MAX(tmp, block);
    } else {
        /* One of each */
        if (dummy) {
            /* If we are adding a dummy, it goes at the end */
            txn->lock[1] = block;
        } else {
            /* New entry goes at the start */
            tmp = txn->lock[0];
            txn->lock[0] = block;
            txn->lock[1] = tmp;
        }
    }
    txn->nlocks++;
    if (!dummy) {
        txn->nreal++;
    }
}

/*
 * Add a capability to the transaction. If it is adjacent to another span in
 * the same block then extend the span. It will add a real lock.
 */
static inline QEMU_ALWAYS_INLINE void
cheri_tag_txn_add_cap(CheriTagTxn *txn, CheriTagBlock *block, size_t index)
{
    size_t i;
    for (i = 0; i < txn->nspans; i++) {
        if (block == txn->span[i].block) {
            if (index == txn->span[i].end) {
                /* We have two consecutive capabilities, so extend the span */
                txn->span[i].end++;
                return;
            }
        }
    }
    /* This will be a new span */
    cheri_debug_assert(txn->nspans < CHERI_TAG_TXN_MAX);
    txn->span[txn->nspans].block = block;
    txn->span[txn->nspans].first = index;
    txn->span[txn->nspans].end = index + 1;
    cheri_tag_txn_add_lock(txn, block, false);
    txn->nspans++;
}

/*
 * Locks the recorded blocks in array order, store each tagblock_lock_write
 * in lock_seq, then set LOCKED.
 */
static inline QEMU_ALWAYS_INLINE void cheri_tag_txn_lock(CheriTagTxn *txn)
{
    CheriTagTxnCpu *cpu_txn;
    size_t i, j;
    bool covered = true;

    if (txn->flags & CHERI_TAG_TXN_FAILED) {
        return;
    }
    if (txn->cpu != NULL && (txn->flags & CHERI_TAG_TXN_VIRT)) {
        cpu_txn = &txn->cpu->cheri_txn;
        if (cpu_txn->active) {
            /*
             * If the transaction we are locking is already covered by the held
             * locks then we just do a join instead.
             */
            for (i = 0; i < txn->nreal; i++) {
                covered = false;
                for (j = 0; j < cpu_txn->nheld; j++) {
                    if (txn->lock[i] == cpu_txn->held[j]) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) {
                    break;
                }
            }
            if (covered) {
                txn->flags |= CHERI_TAG_TXN_JOINED;
                return;
            } else {
                cheri_debug_assert(false);
            }
        }
    }
    for (i = 0; i < txn->nlocks; i++) {
        txn->lock_seq[i] = tagblock_lock_write(txn->lock[i]);
    }
    txn->flags |= CHERI_TAG_TXN_LOCKED;
    if (txn->cpu != NULL && (txn->flags & CHERI_TAG_TXN_VIRT)) {
        cheri_tag_txn_cpu_register(txn);
    }
}

/* Unlock the transactions held locks */
static inline QEMU_ALWAYS_INLINE void cheri_tag_txn_commit(CheriTagTxn *txn)
{
    size_t i;
    if (txn->flags & CHERI_TAG_TXN_JOINED) {
        txn->flags &= ~CHERI_TAG_TXN_JOINED;
        return;
    }
    if (!(txn->flags & CHERI_TAG_TXN_LOCKED)) {
        return;
    }
    /* Unlock is in reverse order */
    for (i = txn->nlocks; i > 0; --i) {
        tagblock_unlock_write_fast(txn->lock[i - 1], txn->lock_seq[i - 1]);
    }
    txn->flags &= ~CHERI_TAG_TXN_LOCKED;
    if (txn->cpu != NULL && (txn->flags & CHERI_TAG_TXN_VIRT)) {
        cheri_tag_txn_cpu_unregister(txn);
    }
}

/* Lock for the conditional store operations */
static inline QEMU_ALWAYS_INLINE bool
cheri_tag_txn_lock_reserved(CheriTagTxn *txn, void *resblock,
                            unsigned long res_seq)
{
    unsigned long old_seq;
    cheri_debug_assert((txn->flags & CHERI_TAG_TXN_FAILED) == 0);
    if (txn->nspans == 0) {
        if (resblock) {
            /*
             * The reservation was for tagged memory which has since been
             * remapped.
             */
            txn->flags |= CHERI_TAG_TXN_FAILED;
            return false;
        } else {
            /* We are using dummy locks */
            cheri_tag_txn_lock(txn);
            return true;
        }
    }
    if (txn->nspans == 1 && txn->nlocks == 1) {
        /* Real load reservation on tagged memory*/
        if (txn->span[0].block != resblock || (res_seq & 1)) {
            /* Reservation taken when the block was locked */
            txn->flags |= CHERI_TAG_TXN_FAILED;
            return false;
        }
        /* Attempt to update the sequence */
        old_seq = qatomic_cmpxchg(&((CheriTagBlock *)resblock)->seqlock,
                                  res_seq, res_seq + 1);
        if (old_seq != res_seq) {
            /* Update failed, sequence already moved on */
            txn->flags |= CHERI_TAG_TXN_FAILED;
            return false;
        }
        /* Orders access to store that makes seqlock odd. */
        smp_wmb();
        txn->lock_seq[0] = res_seq + 1;
        txn->flags |= CHERI_TAG_TXN_LOCKED;
        if (txn->cpu != NULL && (txn->flags & CHERI_TAG_TXN_VIRT)) {
            cheri_tag_txn_cpu_register(txn);
        }
        return true;
    }
    g_assert_not_reached();
}

static inline QEMU_ALWAYS_INLINE void
cheri_tag_txn_init_cap(CheriTagTxn *txn, CPUState *cpu, CheriTagBlock *block,
                       target_ulong vaddr, void *host)
{
    cheri_debug_assert(block);
    cheri_tag_txn_init(txn, cpu, CHERI_TAG_TXN_VIRT);
    txn->host = host;
    txn->addr = vaddr;
    cheri_tag_txn_add_cap(txn, block, page_vaddr_to_tag_offset(vaddr));
}

/* Cheri transaction API resolve functions */

bool cheri_tag_txn_resolve(CheriTagTxn *txn, CPUArchState *env,
                           target_ulong vaddr, int size, int mmu_idx,
                           bool non_fault, uintptr_t pc);

void cheri_tag_txn_resolve_cap(CheriTagTxn *txn, CPUArchState *env,
                               target_ulong vaddr, int reg, int mmu_idx,
                               uintptr_t pc);
void cheri_tag_txn_resolve_phys(CheriTagTxn *txn, CPUArchState *env,
                                RAMBlock *ram, ram_addr_t offset, size_t *len);

void cheri_tag_txn_resolve_phys_store(CheriTagTxn *txn, CPUArchState *env,
                                      RAMBlock *ram, ram_addr_t offset,
                                      size_t size);

void cheri_tag_store_begin(CPUArchState *env, target_ulong vaddr, int size,
                           int mmu_idx, uintptr_t pc);

void cheri_tag_store_end_clear(CPUArchState *env, target_ulong vaddr, int size,
                               int mmu_idx, uintptr_t pc);

void cheri_tag_store_end(CPUArchState *env);

bool cheri_tag_sc_begin(CheriTagTxn *txn, CPUArchState *env, target_ulong vaddr,
                        int reg, bool tag, uintptr_t pc, int mmu_idx,
                        void *res_tagmem, unsigned long res_seq, int ncaps);

void cheri_tag_sc_end(CheriTagTxn *txn);

/* Clear a range of tags. If logging is enabled log the non zero tags first */
static inline QEMU_ALWAYS_INLINE void cheri_tag_txn_clear_tags(CheriTagTxn *txn)
{
    int nspan;
    if ((txn->flags & CHERI_TAG_TXN_FAILED)) {
        return;
    }
    if (unlikely(txn->flags & CHERI_TAG_TXN_LOG) &&
        !(txn->flags & CHERI_TAG_TXN_VIRT)) {
        cheri_tag_txn_log_tags(txn);
    }
    for (nspan = 0; nspan < txn->nspans; nspan++) {
        if (txn->span[nspan].end == txn->span[nspan].first + 1) {
            /* If a span covers a single capability we can clear it faster */
            __tagblock_tag_clear(txn->span[nspan].block,
                                 txn->span[nspan].first);
        } else {
            cheri_tag_block_clear_range(txn->span[nspan].block,
                                        txn->span[nspan].first,
                                        txn->span[nspan].end);
        }
    }
}

/* Cheri transaction API mutate functions */

/* Do a single tag clear transaction */
static inline QEMU_ALWAYS_INLINE void cheri_tag_txn_clear_tag(CheriTagTxn *txn)
{
    if (txn->nspans == 0 || (txn->flags & CHERI_TAG_TXN_FAILED)) {
        return;
    }
    cheri_debug_assert(txn->nspans == 1);
    cheri_debug_assert(txn->span[0].end == txn->span[0].first + 1);
    __tagblock_tag_clear(txn->span[0].block, txn->span[0].first);
}

/* Do a single tag set transaction */
static inline QEMU_ALWAYS_INLINE void cheri_tag_txn_set_tag(CheriTagTxn *txn)
{
    if (txn->nspans == 0 || (txn->flags & CHERI_TAG_TXN_FAILED)) {
        return;
    }
    cheri_debug_assert(txn->nspans == 1);
    cheri_debug_assert(txn->span[0].end == txn->span[0].first + 1);
    if (txn->flags & CHERI_TAG_TXN_LOG) {
        cheri_tag_txn_log_tag_write(txn);
    }
    __tagblock_tag_set(txn->span[0].block, txn->span[0].first);
}

static inline void
cheri_tag_txn_begin_phys_write(CheriTagTxn *txn, CPUArchState *env,
                               RAMBlock *ram, ram_addr_t offset, size_t *len)
{
    cheri_tag_txn_resolve_phys(txn, env, ram, offset, len);
    cheri_tag_txn_lock(txn);
    cheri_tag_txn_clear_tags(txn);
}

#endif /* TARGET_CHERI */
#endif /* CHERI_TAG_TXN_H */

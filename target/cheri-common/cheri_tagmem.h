/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2015-2016 Stacey Son <sson@FreeBSD.org>
 * Copyright (c) 2016-2018 Alfredo Mazzinghi <am2419@cl.cam.ac.uk>
 * Copyright (c) 2016-2018 Alex Richardson <Alexander.Richardson@cl.cam.ac.uk>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#pragma once

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "exec/exec-all.h"
#include "exec/memory.h"

#if defined(TARGET_CHERI)

/*
 * Clear the tags of a RAM range from outside a CPU, locking each block once.
 * Skip pages without tags.
 */
void cheri_tag_phys_clear_range(RAMBlock *ram, ram_addr_t ram_offset,
                                ram_addr_t len);

/*
 * Usage notes:
 * Tag writes go through the tag write transaction API (cheri_tag_txn.h).
 * Readers take a snapshot with tagblock_begin_read/tagblock_end_read.
 */

/*
 * A tagblock covers one target page. The TARGET_PAGE_SIZE can vary at runtime.
 * Size the bitmaps for the largest page size supported (4K).
 * A smaller page only uses the first TAGS_PER_PAGE bits.
 * cheri_tag_init() checks that the page fits.
 */
#define CHERI_TAG_BLOCK_MAX_SIZE 4096
#define TAGS_PER_BLOCK_MAX       (CHERI_TAG_BLOCK_MAX_SIZE / CHERI_CAP_SIZE)
#define CHERI_TAG_BLOCK_SIZE     TARGET_PAGE_SIZE
#define TAGS_PER_PAGE            (CHERI_TAG_BLOCK_SIZE / CHERI_CAP_SIZE)

typedef struct CheriTagBlock_s {
    unsigned long seqlock;
    DECLARE_BITMAP(tag_bitmap, TAGS_PER_BLOCK_MAX);
    /*
     * Bitmap indicating capabilities currently in a device writable mapping
     * written by DMA add/remove functions.
     */
    DECLARE_BITMAP(dma_bitmap, TAGS_PER_BLOCK_MAX);
    /*
     * Pointer to TAGS_PER_PAGE sized array, each entry contains the number of
     * additional mappings covering it
     * Written by DMA add/remove functions, allocated on first overlap. Freed
     * when no more capabilities are marked.
     */
    uint16_t *num_overlaps;
} __attribute__((aligned(64))) CheriTagBlock;

/* Reader snapshot of a tag block */
struct tag_reader_lock_s {
    CheriTagBlock *tagmem;
    unsigned long seq;
    bool success;
    bool started; /* cheri_tag_get has begun the read */
};
typedef struct tag_reader_lock_s tag_reader_lock_t;

static inline QEMU_ALWAYS_INLINE void
tagblock_unlock_write(CheriTagBlock *block)
{
    unsigned long seq = qatomic_read(&block->seqlock);
    /*
     * Orders access to tag_bitmap stores before the store that makes seqlock
     * even again. Pairs with reader smp_rmb in tagblock_begin_read and
     * tagblock_end_read. barrier() prevents compiler reordering.
     */
    smp_wmb();
    barrier();
    qatomic_set(&block->seqlock, seq + 1);
}

static inline QEMU_ALWAYS_INLINE void
tagblock_unlock_write_fast(CheriTagBlock *block, unsigned long seq)
{
    /*
     * Orders access to tag_bitmap stores before the store that makes seqlock
     * even again. Pairs with reader smp_rmb in tagblock_begin_read and
     * tagblock_end_read. barrier() prevents compiler reordering.
     */
    smp_wmb();
    barrier();
    qatomic_set(&block->seqlock, seq + 1);
}

static inline QEMU_ALWAYS_INLINE unsigned long
tagblock_lock_write(CheriTagBlock *block)
{
    unsigned long ret;

    while (true) {
        ret = qatomic_fetch_or(&block->seqlock, 1UL);
        if (likely((ret & 1UL) == 0)) {
            break;
        }
        cpu_relax();
    }
    /*
     * Orders access to store that makes seqlock odd before the following
     * tag_bitmap stores. Pairs with reader smp_rmb in tagblock_begin_read
     * and tagblock_end_read.
     */
    smp_wmb();
    barrier();

    return ret + 1;
}

static inline QEMU_ALWAYS_INLINE bool tagblock_begin_read(CheriTagBlock *block,
                                                          unsigned long *seqp)
{
    unsigned long seq = qatomic_read(&block->seqlock);
    /*
     * Orders access to seqlock before the tag_bitmap loads; pairs with writer's
     * smp_wmb.
     */
    smp_rmb();
    barrier();

    (*seqp) = seq;

    return (seq % 2) == 0;
}

static inline QEMU_ALWAYS_INLINE bool tagblock_end_read(CheriTagBlock *block,
                                                        unsigned long start_seq)
{
    /*
     * Orders access to seqlock re-read after tag_bitmap loads, pairs with
     * writer's smp_wmb.
     */
    smp_rmb();
    barrier();

    unsigned long seq = qatomic_read(&block->seqlock);

    return seq == start_seq;
}

static inline QEMU_ALWAYS_INLINE target_ulong
page_vaddr_to_tag_offset(target_ulong addr)
{
    return (addr & ~TARGET_PAGE_MASK) / CHERI_CAP_SIZE;
}

static inline QEMU_ALWAYS_INLINE bool __tagblock_tag_get(CheriTagBlock *block,
                                                         size_t index)
{
    unsigned long *p = (unsigned long *)block->tag_bitmap + BIT_WORD(index);
    unsigned long *d = (unsigned long *)block->dma_bitmap + BIT_WORD(index);
    /*
     * DMA bits and tag bits only change when a writer holds a lock. So
     * if a change happens during the read, the caller's tagblock_end_read will
     * see this and retry.
     */
    unsigned long word = qatomic_read(p);
    unsigned long result_word = ~qatomic_read(d) & word;
    return ((result_word & BIT_MASK(index)) != 0);
}

static inline QEMU_ALWAYS_INLINE void __tagblock_tag_set(CheriTagBlock *block,
                                                         size_t index)
{
    unsigned long *p = (unsigned long *)block->tag_bitmap + BIT_WORD(index);
    unsigned long word = qatomic_read(p) | BIT_MASK(index);

    qatomic_set(p, word);
}

static inline QEMU_ALWAYS_INLINE void __tagblock_tag_clear(CheriTagBlock *block,
                                                           size_t index)
{
    unsigned long *p = (unsigned long *)block->tag_bitmap + BIT_WORD(index);
    unsigned long word = qatomic_read(p) & ~BIT_MASK(index);

    qatomic_set(p, word);
}

/* Helpers to access the dma_bitmap */
static inline QEMU_ALWAYS_INLINE bool __tagblock_dma_get(CheriTagBlock *block,
                                                         size_t index)
{
    unsigned long *d = (unsigned long *)block->dma_bitmap + BIT_WORD(index);
    unsigned long word = qatomic_read(d);
    return ((word & BIT_MASK(index)) != 0);
}

static inline QEMU_ALWAYS_INLINE void __tagblock_dma_set(CheriTagBlock *block,
                                                         size_t index)
{
    unsigned long *d = (unsigned long *)block->dma_bitmap + BIT_WORD(index);
    unsigned long word = qatomic_read(d) | BIT_MASK(index);

    qatomic_set(d, word);
}

static inline QEMU_ALWAYS_INLINE void __tagblock_dma_clear(CheriTagBlock *block,
                                                           size_t index)
{
    unsigned long *d = (unsigned long *)block->dma_bitmap + BIT_WORD(index);
    unsigned long word = qatomic_read(d) & ~BIT_MASK(index);

    qatomic_set(d, word);
}

/*
 * Transaction handler called on an exception.
 * A longjmp will occur losing the stack so we must release the locks that are
 * held using the state in the cpu.
 */
void cheri_tag_txn_exception_thrown(CPUState *cpu);

static inline void cheri_tag_locks_exception_thrown(CPUState *cpu)
{
    cheri_tag_txn_exception_thrown(cpu);
}

void cheri_tag_init(MemoryRegion *mr, uint64_t memory_size);

/* Get a single tag */

bool cheri_tag_get(CPUArchState *env, target_ulong vaddr, int reg,
                   hwaddr *ret_paddr, int *prot, uintptr_t pc, int mmu_idx,
                   void *host_addr, void *tagmem, tag_reader_lock_t *lock);

/*
 * Get and set many do not take any locks, as they are not meant for use in
 * conjunction with modififying data. They are atomic where possible, but
 * no guarantee is made
 * Get/set many currently don't have an mmu_idx because no targets currently
 * require it.
 */

int cheri_tag_get_many(CPUArchState *env, target_ulong vaddr, int reg,
                       hwaddr *ret_paddr, uintptr_t pc);
void cheri_tag_set_many(CPUArchState *env, uint32_t tags, target_ulong vaddr,
                        int reg, hwaddr *ret_paddr, uintptr_t pc);

void *cheri_tagmem_for_addr(CPUArchState *env, target_ulong vaddr,
                            RAMBlock *ram, ram_addr_t ram_offset, size_t size,
                            int *prot, bool tag_write);

/* Helpers for writes to tagged RAM */

/*
 * Lock a tagblock for write and clear the tags the write will cover.
 * The updated len indicates how many bytes were invalidated (limited by the
 * tagblock length).
 * Returns the locked tagblock, else NULL if the associated memory has no tags.
 */
void *cheri_tag_phys_invalidate_begin(CPUArchState *env, RAMBlock *ram,
                                      ram_addr_t ram_offset, size_t *len);

/* Releases the locked tagblock, does nothing if it is null */
void cheri_tag_phys_invalidate_end(void *tagblock);

/*
 * Copy a host buffer into tagged memory clearing tags
 * Copies the memory from src to dst in chunks the size that a tagblock
 * can cover. Acquires a tag block and clears the tags in it if the memory has
 * tags, else just copies.
 */
void cheri_phys_memcpy(CPUArchState *env, RAMBlock *ram, ram_addr_t ram_offset,
                       uint8_t *dst, const uint8_t *src, size_t len);

/*
 * Used to begin the invalidation of tags for a range that can cover more than
 * a single block. Sets the dma bits for the whole range so that reads of
 * the range will get clear tags. Handles overlapping ranges by counting
 * the number of overlaps. If there are more overlaps than can be represented
 * in the structure, returns with len == 0 (never a shortened value).
 * If *len is zero then nothing was marked and end must not be called.
 */
void cheri_tag_phys_invalidate_range_begin(RAMBlock *ram, ram_addr_t offset,
                                           size_t *len);

/*
 * Clears the tags for an address range that can cover more than a block.
 * The range passed must match that used for the begin operation.
 * Len must be the value that begin left in *len, access_len is the number of
 * bytes the device wrote clamped to len.
 * The tags of the capabilities the written bytes touch are cleared, and the
 * whole mapping is removed. Passing a range that doesn't match what was given
 * to begin results in tags being hidden for good, or makes them visible too
 * early.
 */
void cheri_tag_phys_invalidate_range_end(RAMBlock *ram, ram_addr_t offset,
                                         size_t len, size_t access_len);

/**
 * Fetch a single tag for use by the debug stub.
 */
bool cheri_tag_get_debug(RAMBlock *ram, ram_addr_t ram_offset);
#else /* !TARGET_CHERI */

#define cheri_tag_locks_exception_thrown(...)

#endif /* TARGET_CHERI */

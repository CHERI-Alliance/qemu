/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2015-2016 Stacey Son <sson@FreeBSD.org>
 * Copyright (c) 2016-2018 Alfredo Mazzinghi <am2419@cl.cam.ac.uk>
 * Copyright (c) 2016-2018 Alex Richardson <Alexander.Richardson@cl.cam.ac.uk>
 * Copyright (c) 2021-2021 Lawrence Esswood <le277@cam.ac.uk>
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
#include "cheri_tagmem.h"
#include "cheri_tag_txn.h"
#include "exec/exec-all.h"
#include "exec/log.h"
#include "exec/ramblock.h"
#include "hw/boards.h"
#include "cheri_defs.h"
#include "cheri-helper-utils.h"
// XXX: use hbitmap? Or a different data structure?
#include "qemu/bitmap.h"
#include "glib/ghash.h"

#if defined(TARGET_MIPS)
#include "cheri_utils.h"
#include "internal.h"
#endif

#if !defined(TARGET_CHERI)
#error "Should only be included for TARGET_CHERI"
#endif

#ifdef TARGET_MIPS
#define store_capcause_reg(env, reg) cpu_mips_store_capcause_reg(env, reg)
#define clear_capcause_reg(env)      cpu_mips_clear_capcause_reg(env)
#else
#define store_capcause_reg(env, reg)
#define clear_capcause_reg(env)
#endif

/*
 * Tagged Memory Emulation
 *
 * CHERI requires a 1-bit tag for every capability-aligned,
 * capability-sized word in physical memory.  This allows capabilities
 * to be safely loaded and stored in meory without loss of integrity.
 *
 * FIXME: rewrite using somethign more like the upcoming MTE changes
 * (https://github.com/rth7680/qemu/commits/tgt-arm-mte-user)
 *
 * I/O threads are also protected. Writes clear tags as required under the tag
 * block's write lock. A device mapping hides tags until the unmap which will
 * invalidate the tags.
 */

#ifndef CAP_TAG_GET_MANY_SHFT
#error "Define a CAP_TAG_GET_MANY_SHFT in appropriate cheri-archspecific.h"
#endif

_Static_assert((1 << CAP_TAG_GET_MANY_SHFT) <= BITS_PER_LONG, "");

#define CAP_TAG_GET_MANY_MASK                                                  \
    (~0UL >> (BITS_PER_LONG - (1UL << CAP_TAG_GET_MANY_SHFT)))
#define CAP_TAG_MANY_DATA_SIZE (CHERI_CAP_SIZE << CAP_TAG_GET_MANY_SHFT)

static inline size_t num_tagblocks(RAMBlock* ram)
{
    uint64_t memory_size = memory_region_size(ram->mr);
    size_t result = DIV_ROUND_UP(memory_size, CHERI_CAP_SIZE * TAGS_PER_PAGE);
    if (memory_size != result * CHERI_CAP_SIZE * TAGS_PER_PAGE) {
        warn_report_once("WARNING: memory region %s size %" PRIu64
                         " is not a multiple of tag block size %d\r",
                         memory_region_name(ram->mr), memory_size,
                         CHERI_CAP_SIZE * TAGS_PER_PAGE);
    }
    return result;
}

/*
 * TAGBLOCK_CHUNK_SIZE doesn't need to be a multiple of the block size.
 * The tail of a chunk can be left unused.
 */
#define TAGBLOCK_CHUNK_SIZE 0x4000
QEMU_BUILD_BUG_ON(sizeof(CheriTagBlock) > TAGBLOCK_CHUNK_SIZE);

static __thread CheriTagBlock *tag_alloc_data;
static __thread unsigned int tag_alloc_used;

static CheriTagBlock *cheri_tag_block_alloc(RAMBlock *ram, uint64_t tagidx)
{
    CheriTagBlock *tagblk, *old;

    if (tag_alloc_data == NULL ||
        tag_alloc_used == TAGBLOCK_CHUNK_SIZE / sizeof(CheriTagBlock)) {
        tag_alloc_used = 0;
        tag_alloc_data = mmap(NULL, TAGBLOCK_CHUNK_SIZE, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANON, 0, 0);
        if (tag_alloc_data == MAP_FAILED) {
            error_report("Can't allocate tag block.");
            exit(1);
        }
    }
    tagblk = tag_alloc_data + tag_alloc_used;
    tag_alloc_used++;

    CheriTagBlock **tagmem = (CheriTagBlock **)ram->cheri_tags;
    size_t tagblock_index = tagidx / TAGS_PER_PAGE;
    /* Possible race here so use atomic compare and swap. */
    cheri_debug_assert(tagblock_index < num_tagblocks(ram) &&
                       "Tag index out of bounds");
    old = qatomic_cmpxchg(&tagmem[tagblock_index], NULL, tagblk);
    if (old != NULL) {
        /* Lost the race, free and give back the block we just grabbed. */
        tag_alloc_used--;
        return old;
    } else {
        return tagblk;
    }
}

static inline QEMU_ALWAYS_INLINE CheriTagBlock *
cheri_tag_block_at(size_t tag_index, RAMBlock *ram)
{
    const size_t tagbock_index = tag_index / TAGS_PER_PAGE;
    cheri_debug_assert(ram->cheri_tags);
    if (tagbock_index >= num_tagblocks(ram)) {
        error_report("Call to access tag out of bounds");
        return NULL;
    }
    CheriTagBlock **tagmem = (CheriTagBlock **)ram->cheri_tags;
    /* pairs with qatomic_cmpxchg in cheri_tag_block_alloc */
    return qatomic_load_acquire(&tagmem[tagbock_index]);
}

static CheriTagBlock *cheri_tag_block_find_or_alloc(RAMBlock *ram,
                                                    ram_addr_t ram_offset)
{
    CheriTagBlock *tagblk;
    size_t start_idx = ram_offset / CHERI_CAP_SIZE;
    tagblk = cheri_tag_block_at(start_idx, ram);
    if (!tagblk) {
        /* Attempt to allocate a new tagblock */
        tagblk = cheri_tag_block_alloc(ram, start_idx);
    }
    return tagblk;
}

static inline QEMU_ALWAYS_INLINE int tagmem_get_tag_many(CheriTagBlock *tagmem,
                                                         target_ulong vaddr)
{
    unsigned long res, not_dma;
    unsigned long index = page_vaddr_to_tag_offset(vaddr);
    while (1) {
        unsigned long seq;
        if (!tagblock_begin_read(tagmem, &seq)) {
            cpu_relax();
            continue;
        }
        /* Mask so that a tag reads as set only when its dma bit is clear. */
        not_dma = ~qatomic_read(tagmem->dma_bitmap + BIT_WORD(index));
        res = qatomic_read(tagmem->tag_bitmap + BIT_WORD(index)) & not_dma;
        if (tagblock_end_read(tagmem, seq)) {
            break;
        }
        cpu_relax();
    }

    res >>= (index % BITS_PER_LONG);
    res &= CAP_TAG_GET_MANY_MASK;

    return res;
}

/* A 1 in the lowest bit in each byte of a packed_t */
#define GET_SET_MANY_LOCKTAG_TAG_MASK (((packed_t)~0ULL) / 0xFF)

static inline QEMU_ALWAYS_INLINE void tagmem_set_tag_many(CheriTagBlock *tagmem,
                                                          target_ulong vaddr,
                                                          unsigned long tags)
{
    unsigned long index = page_vaddr_to_tag_offset(vaddr);

    tagblock_lock_write(tagmem);
    unsigned long *p = (unsigned long *)tagmem->tag_bitmap + BIT_WORD(index);
    unsigned long word = qatomic_read(p);
    uint8_t shift = index % BITS_PER_LONG;
    unsigned long mask = CAP_TAG_GET_MANY_MASK << shift;
    word = (word & ~mask) | ((tags << shift) & mask);
    qatomic_set(p, word);
    tagblock_unlock_write(tagmem);
}

void cheri_tag_init(MemoryRegion *mr, uint64_t memory_size)
{
    assert(memory_region_is_ram(mr));
    assert(memory_region_size(mr) == memory_size &&
           "Incorrect tag mem size passed?");
    assert(mr->ram_block->cheri_tags == NULL && "Already initialized?");

    assert(TARGET_PAGE_SIZE <= CHERI_TAG_BLOCK_MAX_SIZE);
    size_t cheri_ntagblks = num_tagblocks(mr->ram_block);
    mr->ram_block->cheri_tags =
        g_malloc0(cheri_ntagblks * sizeof(CheriTagBlock *));
    if (mr->ram_block->cheri_tags == NULL) {
        error_report("%s: Can't allocated tag memory", __func__);
        exit(-1);
    }
}

/*
 * Returns the tagblock for a TLB fill, allocating it on first use, or
 * NULL for memory without tags.
 * A fill of tagged RAM always allocates the block, so no TLB entry for
 * tagged RAM is missing it and no flush is needed.
 */
void *cheri_tag_block_for_fill(CPUArchState *env, target_ulong vaddr,
                               RAMBlock *ram, ram_addr_t ram_offset,
                               size_t size, int *prot, bool tag_write)
{

    if (unlikely(!ram || !ram->cheri_tags)) {
        /* Tags stored here are effectively cleared (unless they should trap) */
        if (!(*prot & PAGE_SC_TRAP)) {
            *prot |= PAGE_SC_CLEAR;
        }
        if (tag_write) {
            error_report("Attempting change tag bit on memory without tags:");
            error_report("%s: vaddr=0x%jx -> %s+0x%jx", __func__,
                         (uintmax_t)vaddr, ram ? ram->idstr : NULL,
                         (uintmax_t)ram_offset);
        }
        return NULL;
    }

#ifndef TARGET_AARCH64
    // AArch64 seems to use different sizes. Might be worth looking into.
    cheri_debug_assert(ram_offset % TARGET_PAGE_SIZE == 0 &&
                       "Unexpected offset");
    cheri_debug_assert(size == TARGET_PAGE_SIZE && "Unexpected size");
#endif
    return cheri_tag_block_find_or_alloc(ram, ram_offset);
}

/*
 * Given a ram_block address range (block start address, range start address
 * and length in bytes), return the index within the block of the start/end
 * clipping to the block's start and end addresses. If the range doesn't fall
 * in the block's range return false.
 * block_start and range_offset are both offsets within the RAM block (not
 * addresses). The end parameter is exclusive of the range.
 */
static bool tag_idx_for_range(ram_addr_t block_start, ram_addr_t range_offset,
                              size_t len, size_t *first, size_t *end)
{
    ram_addr_t clipped_start = MAX(block_start, range_offset);
    ram_addr_t clipped_end =
        MIN(block_start + CHERI_TAG_BLOCK_SIZE, range_offset + len);

    if (clipped_start >= clipped_end) {
        /* Outside range of the block */
        return false;
    }
    *first = (clipped_start - block_start) / CHERI_CAP_SIZE;
    *end = DIV_ROUND_UP(clipped_end - block_start, CHERI_CAP_SIZE);
    return true;
}

typedef enum {
    ITER_OP_INVAL = 0, /* Prevent 0 initialised ops appearing valid */
    ITER_OP_SET,
    ITER_OP_CLEAR,
    ITER_OP_ANYSET,
} Bit_Iter_Op;

/*
 * Iterate over a range of a dma or tag bitmap and run the provided operation.
 * Range is specified in block local indices.
 * Assumes a lock is held for the resource.
 * The operation can be ITER_OP_SET, ITER_OP_CLEAR or ITER_OP_ANYSET
 * Return value only meaningful for anyset where it indicates if any bits are
 * set in the target range.
 */
static bool bitmap_range_iter(unsigned long *bitmap, size_t start_idx,
                              size_t end_idx, Bit_Iter_Op op)
{

    unsigned long mask_operand; /* Mask representing bits to set/clear/test */
    unsigned long word;
    int nr, bits_to_clear;
    unsigned long res = 0;

    mask_operand = BITMAP_FIRST_WORD_MASK(start_idx);
    nr = end_idx - start_idx;
    bits_to_clear = BITS_PER_LONG - (start_idx % BITS_PER_LONG);
    word = BIT_WORD(start_idx);
    while (nr - bits_to_clear >= 0) {
        switch (op) {
        case ITER_OP_CLEAR:
            qatomic_set(&bitmap[word],
                        qatomic_read(&bitmap[word]) & ~mask_operand);
            break;
        case ITER_OP_SET:
            qatomic_set(&bitmap[word],
                        qatomic_read(&bitmap[word]) | mask_operand);
            break;
        case ITER_OP_ANYSET:
            /*
             * Only holders of the lock call this function, so we are
             * fine to do a plain read here.
             */
            res |= (bitmap[word] & mask_operand);
            break;
        default:
            g_assert_not_reached();
        }
        nr -= bits_to_clear;
        bits_to_clear = BITS_PER_LONG;
        mask_operand = ~0UL;
        word++;
    }
    if (nr) {
        mask_operand &= BITMAP_LAST_WORD_MASK(end_idx);
        switch (op) {
        case ITER_OP_CLEAR:
            qatomic_set(&bitmap[word],
                        qatomic_read(&bitmap[word]) & ~mask_operand);
            break;
        case ITER_OP_SET:
            qatomic_set(&bitmap[word],
                        qatomic_read(&bitmap[word]) | mask_operand);
            break;
        case ITER_OP_ANYSET:
            res |= (bitmap[word] & mask_operand);
            break;
        default:
            g_assert_not_reached();
        }
    }
    return res != 0;
}

/*
 * Add a DMA mapping for the supplied range of the provided block.
 * Caller holds the tagblock lock.
 */
static bool add_block_dma_mapping(CheriTagBlock *tagblk, size_t start,
                                  size_t end)
{
    if (!bitmap_range_iter(tagblk->dma_bitmap, start, end, ITER_OP_ANYSET)) {
        /* No bits in the requested range are set */
        bitmap_range_iter(tagblk->dma_bitmap, start, end, ITER_OP_SET);
        return true;
    }
    if (tagblk->num_overlaps == NULL) {
        tagblk->num_overlaps = g_new0(uint16_t, TAGS_PER_PAGE);
    }
    /* Check pass, do this first so if we fail nothing changed */
    for (size_t idx = start; idx < end; idx++) {
        if (test_bit(idx, tagblk->dma_bitmap) &&
            tagblk->num_overlaps[idx] == UINT16_MAX) {
            return false;
        }
    }
    /*
     * Apply pass.
     * Set unset bits.
     * If bits are already set then increment its counter
     */
    for (size_t idx = start; idx < end; idx++) {
        if (test_bit(idx, tagblk->dma_bitmap)) {
            tagblk->num_overlaps[idx]++;
        } else {
            __tagblock_dma_set(tagblk, idx);
        }
    }
    return true;
}

/*
 * Remove the DMA mapping for the range in the given block.
 * Reduce the overlap count for tags that have one, or unset the bit for
 * those that do not.
 */
static void remove_block_dma_mapping(CheriTagBlock *tagblk, size_t start,
                                     size_t end)
{
    for (size_t idx = start; idx < end; idx++) {
        cheri_debug_assert(test_bit(idx, tagblk->dma_bitmap));
    }
    if (!tagblk->num_overlaps) {
        /* No overlaps so we can bulk clear the dma bits */
        bitmap_range_iter(tagblk->dma_bitmap, start, end, ITER_OP_CLEAR);
        return;
    }
    /*
     * Iterate over the range. Clear the dma flag for capabilities that have
     * no overlap. Decrement the count for those that do.
     */
    for (size_t idx = start; idx < end; idx++) {
        if (tagblk->num_overlaps[idx]) {
            tagblk->num_overlaps[idx]--;
        } else {
            __tagblock_dma_clear(tagblk, idx);
        }
    }
    if (!bitmap_range_iter(tagblk->dma_bitmap, 0, TAGS_PER_PAGE,
                           ITER_OP_ANYSET)) {
        g_free(tagblk->num_overlaps);
        tagblk->num_overlaps = NULL;
    }
}

void cheri_tag_phys_invalidate_range_begin(RAMBlock *ram, ram_addr_t offset,
                                           size_t *len)
{
    CheriTagBlock *tagblk;
    ram_addr_t blk_start, blk_end;
    size_t requested_len;
    size_t first = 0, last = 0;
    if (ram->cheri_tags == NULL || len == NULL || *len == 0) {
        return;
    }
    requested_len = *len;
    /*
     * Iterate over each tagblock in the range.
     * Start is offset rounded down
     * end is offset + len unrounded.
     */
    for (ram_addr_t blk = blk_start =
             QEMU_ALIGN_DOWN(offset, CHERI_TAG_BLOCK_SIZE);
         blk < offset + *len; blk += CHERI_TAG_BLOCK_SIZE) {
        tagblk = cheri_tag_block_at(blk / CHERI_CAP_SIZE, ram);
        if (!tagblk) {
            /* Attempt to allocate a new tagblock */
            tagblk = cheri_tag_block_alloc(ram, blk / CHERI_CAP_SIZE);
        }
        tagblock_lock_write(tagblk);
        tag_idx_for_range(blk, offset, *len, &first, &last);
        if (!add_block_dma_mapping(tagblk, first, last)) {
            *len = 0;
            blk_end = blk;
            tagblock_unlock_write(tagblk);
            /*
             * Need to undo the blocks we added.
             * Loop up using the variables we already have
             */
            for (blk = blk_start; blk < blk_end; blk += CHERI_TAG_BLOCK_SIZE) {
                tagblk = cheri_tag_block_at(blk / CHERI_CAP_SIZE, ram);
                tagblock_lock_write(tagblk);
                tag_idx_for_range(blk, offset, requested_len, &first, &last);
                remove_block_dma_mapping(tagblk, first, last);
                tagblock_unlock_write(tagblk);
            }
            return;
        }
        tagblock_unlock_write(tagblk);
    }
}

void cheri_tag_phys_invalidate_range_end(RAMBlock *ram, ram_addr_t offset,
                                         size_t len, size_t access_len)
{
    CheriTagBlock *tagblk;
    size_t first = 0, last = 0;
    if (ram->cheri_tags == NULL || len == 0) {
        return;
    }
    access_len = MIN(len, access_len);
    for (ram_addr_t blk = QEMU_ALIGN_DOWN(offset, CHERI_TAG_BLOCK_SIZE);
         blk < offset + len; blk += CHERI_TAG_BLOCK_SIZE) {
        tagblk = cheri_tag_block_at(blk / CHERI_CAP_SIZE, ram);
        if (!tagblk) {
            g_assert_not_reached();
        }
        tagblock_lock_write(tagblk);
        if (tag_idx_for_range(blk, offset, access_len, &first, &last)) {
            bitmap_range_iter(tagblk->tag_bitmap, first, last, ITER_OP_CLEAR);
        }
        tag_idx_for_range(blk, offset, len, &first, &last);
        remove_block_dma_mapping(tagblk, first, last);
        tagblock_unlock_write(tagblk);
    }
}

static inline void *cheri_tag_block_from_tlb(CPUArchState *env,
                                             target_ulong vaddr, int mmu_idx,
                                             bool isWrite, uintptr_t *flags_out)
{
    /*
     * Find the iotlbentry for ptr.  This *must* be present in the TLB
     * because the caller has just probed the vaddr, so the entry at tlb_index
     * is the one for this access.
     * A fill for less than a page sets TLB_INVALID_MASK so that the next access
     * fills again.
     * We ignore the bit as probe_access_internal does in its own fill.
     * TODO: Perhaps there should be a cputlb helper that returns a
     * matching tlb entry + iotlb entry.
     */
#ifdef CONFIG_DEBUG_TCG
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, vaddr);
    g_assert(tlb_hit((isWrite ? tlb_addr_write(entry) : entry->addr_read) &
                         ~TLB_INVALID_MASK,
                     vaddr));
#endif
    CPUTLBEntryFull *pfull =
        &env_tlb(env)->d[mmu_idx].fulltlb[tlb_index(env, mmu_idx, vaddr)];
    if (isWrite) {
        *flags_out = IOTLB_GET_TAGMEM_FLAGS(pfull, write);
        return IOTLB_GET_TAGMEM(pfull, write);
    } else {
        *flags_out = IOTLB_GET_TAGMEM_FLAGS(pfull, read);
        return IOTLB_GET_TAGMEM(pfull, read);
    }
}

/*
 * Determine the tag block which backs the just-probed vaddr. This function
 * assumes the caller has just probed the vaddr for this access (is_write
 * indicates the access direction), so the TLB entry is present. Returns the
 * page's tag block or NULL for memory without tag storage (MMIO, ROM, RAM
 * without tags), where tags read as 0 and stores lock a dummy block.
 * *flags_out gets the page's TLBENTRYCAP_FLAG_* bits.
 */
static inline QEMU_ALWAYS_INLINE CheriTagBlock *
cheri_tag_block_for_probed(CPUArchState *env, target_ulong vaddr, int mmu_idx,
                           bool is_write, void *probe_block,
                           uintptr_t *flags_out)
{
    if (probe_block) {
        /* If we have a real probed block, return it */
        *flags_out = 0;
        return probe_block;
    }
    return cheri_tag_block_from_tlb(env, vaddr, mmu_idx, is_write, flags_out);
}

/*
 * Resolve the tagblocks required for a data store transaction.
 * This touches at most two capabilities
 */
bool cheri_tag_txn_resolve(CheriTagTxn *txn, CPUArchState *env,
                           target_ulong vaddr, int size, int mmu_idx,
                           bool non_fault, uintptr_t pc)
{
    target_ulong page_first, page_last;
    void *blocks[2];
    void *host[2];
    target_ulong offset[2];
    int access_flags[2];
    int tries = 1;
    int caps = 1;
    uintptr_t tagmem_flags;
    CheriTagBlock *block;

    cheri_debug_assert(size > 0);

    cheri_tag_txn_init(txn, env_cpu(env), CHERI_TAG_TXN_VIRT);
    txn->addr = vaddr;
    offset[0] = QEMU_ALIGN_DOWN(vaddr, CHERI_CAP_SIZE);
    offset[1] = QEMU_ALIGN_DOWN(vaddr + size - 1, CHERI_CAP_SIZE);
    page_first = offset[0] & TARGET_PAGE_MASK;
    page_last = offset[1] & TARGET_PAGE_MASK;

    if (offset[0] != offset[1]) {
        caps = 2;
#if defined(TARGET_ALIGNED_ONLY)
#if defined(CHERI_UNALIGNED)
        if (unlikely(page_first != page_last)) {
            warn_report("Got unaligned %d-byte store across page "
                        "boundary at 0x" TARGET_FMT_lx "\r",
                        size, vaddr);
        }
#else
        if (!non_fault) {
            /*
             * Tags are locked before every store, so this also runs for a
             * misaligned store that is about to raise its alignment exception;
             * only an access that is really carried out is fatal.
             */
            FILE *logfile = qemu_log_trylock();
            error_report("FATAL: %s: " TARGET_FMT_lx
                         "+%d crosses a page boundary\r",
                         __func__, vaddr, size);
            char buffer[256];
            FILE *f = fmemopen(buffer, sizeof(buffer), "w");
            fprintf(f, "Probably caused by guest instruction: ");
            target_disas(f, env_cpu(env), cpu_get_current_pc(env, pc),
                         /* Only one instr*/ -1);
            fprintf(f, "\r");
            fclose(f);
            buffer[sizeof(buffer) - 1] = '\0';
            error_report("%s", buffer);
            qemu_log_unlock(logfile);
            exit(1);
        }
#endif
#endif
        access_flags[0] = probe_access_flags_fast(
            env, offset[0], CHERI_CAP_SIZE, MMU_DATA_STORE, mmu_idx, non_fault,
            &host[0], &blocks[0], pc);
    } else {
        access_flags[0] =
            probe_access_flags_fast(env, vaddr, size, MMU_DATA_STORE, mmu_idx,
                                    non_fault, &host[0], &blocks[0], pc);
    }

    if (access_flags[0] & TLB_INVALID_MASK) {
        txn->flags |= CHERI_TAG_TXN_FAILED;
        txn->host = NULL;
        return false;
    }

    if (caps == 2) {
        if (page_first != page_last) {
            tries = 2;
            /*
             * If we span multiple pages then we may need to cope with
             * tlb evictions. Limit these to 3 attempts total
             */
        }
        while (tries) {
            access_flags[1] = probe_access_flags_fast(
                env, offset[1], CHERI_CAP_SIZE, MMU_DATA_STORE, mmu_idx,
                non_fault, &host[1], &blocks[1], pc);
            if (access_flags[1] & TLB_INVALID_MASK) {
                txn->flags |= CHERI_TAG_TXN_FAILED;
                txn->host = NULL;
                return false;
            }
            /* Check if the probe evicted the first entry */
            if (tlb_hit(tlb_addr_write(tlb_entry(env, mmu_idx, offset[0])),
                        offset[0])) {
                break;
            }

            access_flags[0] = probe_access_flags_fast(
                env, offset[0], CHERI_CAP_SIZE, MMU_DATA_STORE, mmu_idx,
                non_fault, &host[0], &blocks[0], pc);
            if (access_flags[0] & TLB_INVALID_MASK) {
                txn->flags |= CHERI_TAG_TXN_FAILED;
                txn->host = NULL;
                return false;
            }

            /* And then check that didn't evict the second */
            if (tlb_hit(tlb_addr_write(tlb_entry(env, mmu_idx, offset[1])),
                        offset[1])) {
                break;
            }
            tries--;
        }
        if (host[0]) {
            host[0] += (vaddr - offset[0]);
        }
    }
    for (int i = 0; i < caps; i++) {
        block = cheri_tag_block_for_probed(env, offset[i], mmu_idx, true,
                                           blocks[i], &tagmem_flags);
        if (likely(block)) {
            cheri_debug_assert(!(tagmem_flags & TLBENTRYCAP_FLAG_CLEAR));
            cheri_tag_txn_add_cap(txn, block,
                                  page_vaddr_to_tag_offset(offset[i]));
        } else {
            cheri_tag_txn_add_lock(txn, cheri_tag_block_dummy(offset[i]), true);
        }
    }
    txn->host = host[0];
    return true;
}

void cheri_tag_txn_resolve_cap(CheriTagTxn *txn, CPUArchState *env,
                               target_ulong vaddr, int reg, int mmu_idx,
                               uintptr_t pc)
{
    void *tagmem;
    uintptr_t tagmem_flags;
    cheri_debug_assert(QEMU_ALIGN_DOWN(vaddr, CHERI_CAP_SIZE) == vaddr);
    cheri_tag_txn_init(txn, env_cpu(env),
                       qemu_log_instr_enabled(env)
                           ? (CHERI_TAG_TXN_VIRT | CHERI_TAG_TXN_LOG)
                           : CHERI_TAG_TXN_VIRT);
    txn->addr = vaddr;
    store_capcause_reg(env, reg);
    /* Note: this probe will handle any store cap faults */
    void *host_addr =
        probe_cap_write_fast(env, vaddr, CHERI_CAP_SIZE, mmu_idx, pc, &tagmem);
    clear_capcause_reg(env);
    tagmem = cheri_tag_block_for_probed(env, vaddr, mmu_idx, true, tagmem,
                                        &tagmem_flags);
    if (likely(tagmem)) {
        cheri_debug_assert(!(tagmem_flags & TLBENTRYCAP_FLAG_CLEAR));
        cheri_debug_assert(!(tagmem_flags & TLBENTRYCAP_FLAG_TRAP));
        cheri_tag_txn_add_cap(txn, tagmem, page_vaddr_to_tag_offset(vaddr));
    } else {
        cheri_tag_txn_add_lock(txn, cheri_tag_block_dummy(vaddr), true);
    }
    txn->host = host_addr;
}

void cheri_tag_txn_resolve_phys(CheriTagTxn *txn, CPUArchState *env,
                                RAMBlock *ram, ram_addr_t offset, size_t *len)
{
    size_t blk_offset = offset % CHERI_TAG_BLOCK_SIZE;
    void *tagmem;
    size_t first = 0, end = 0;
    cheri_tag_txn_init(txn, env ? env_cpu(env) : NULL,
                       (env && qemu_log_instr_enabled(env)) ? CHERI_TAG_TXN_LOG
                                                            : 0);
    txn->addr = offset;
    *len = MIN(*len, CHERI_TAG_BLOCK_SIZE - blk_offset);
    if (!ram->cheri_tags) {
        return;
    }
    tagmem = cheri_tag_block_find_or_alloc(ram, offset);
    if (*len == 0) {
        /* Just take a lock do not record a span */
        cheri_tag_txn_add_lock(txn, tagmem, false);
    } else {
        tag_idx_for_range(offset - blk_offset, offset, *len, &first, &end);
        txn->span[0].block = tagmem;
        txn->span[0].first = first;
        txn->span[0].end = end;
        txn->nspans = 1;
        cheri_tag_txn_add_lock(txn, tagmem, false);
    }
}

void cheri_tag_txn_resolve_phys_store(CheriTagTxn *txn, CPUArchState *env,
                                      RAMBlock *ram, ram_addr_t offset,
                                      size_t size)
{
    void *tagmem;
    size_t first = 0, end = 0;
    size_t c, idx;
    cheri_debug_assert(size > 0 && size <= CHERI_CAP_SIZE);
    cheri_tag_txn_init(txn, env ? env_cpu(env) : NULL,
                       (env && qemu_log_instr_enabled(env)) ? CHERI_TAG_TXN_LOG
                                                            : 0);
    txn->addr = offset;
    if (!ram->cheri_tags) {
        return;
    }
    first = offset / CHERI_CAP_SIZE;
    end = (offset + size - 1) / CHERI_CAP_SIZE;

    for (c = first; c <= end; c++) {
        /* Expect to pass through this only one or two passes */
        tagmem = cheri_tag_block_find_or_alloc(ram, c * CHERI_CAP_SIZE);
        idx = (c * CHERI_CAP_SIZE % CHERI_TAG_BLOCK_SIZE) / CHERI_CAP_SIZE;
        cheri_tag_txn_add_cap(txn, tagmem, idx);
    }
}

void cheri_tag_txn_log_tags(CheriTagTxn *txn)
{
    int i, span_idx;
    ram_addr_t addr;
    bool old_tag;
    for (span_idx = 0; span_idx < txn->nspans; span_idx++) {
        for (i = txn->span[span_idx].first; i < txn->span[span_idx].end; i++) {
            old_tag = __tagblock_tag_get(txn->span[span_idx].block, i);
            if (old_tag) {
                addr = QEMU_ALIGN_DOWN(txn->addr, CHERI_CAP_SIZE) +
                       (i - txn->span[span_idx].first) * CHERI_CAP_SIZE;
                qemu_log_instr_extra(txn->cpu->env_ptr,
                                     "    Cap Tag ramaddr Write [" RAM_ADDR_FMT
                                     "] %d -> 0\n",
                                     addr, old_tag);
            }
        }
    }
}

void cheri_tag_txn_log_tag_write(CheriTagTxn *txn)
{
    bool old_tag = __tagblock_tag_get(txn->span[0].block, txn->span[0].first);
    qemu_maybe_log_instr_extra(
        txn->cpu->env_ptr,
        "    Cap Tag Write [" TARGET_FMT_lx "/" RAM_ADDR_FMT "] %d -> 1\n",
        txn->addr, qemu_ram_addr_from_host(txn->host), old_tag);
}

void cheri_tag_txn_exception_thrown(CPUState *cpu)
{
    int i;
    CheriTagTxnCpu *txn_cpu = &cpu->cheri_txn;
    for (i = txn_cpu->nheld - 1; i >= 0; i--) {
        /* Release locks in reverse order */
        tagblock_unlock_write(txn_cpu->held[i]);
    }
    txn_cpu->nheld = 0;
    txn_cpu->active = NULL;
    txn_cpu->store_open = false;
}

/*
 * The most common case for a resolve is the simple case i.e.
 * all bytes in one capability and hitting tagged RAM in the TLB.
 * So optimise for that case, return false if the fast probe fails and we must
 * fall back to the slow, general purpose resolve.
 */
static inline QEMU_ALWAYS_INLINE bool
cheri_tag_txn_resolve_fastcap(CPUArchState *env, CheriTagTxnCpu *txn_cpu,
                              target_ulong vaddr, int size, int mmu_idx,
                              uintptr_t pc)
{
    CheriTagTxn *txn = &txn_cpu->store;
    int flags;
    void *host, *block;
    /* Check that the first and last byte are in the same capability */
    if ((vaddr & (CHERI_CAP_SIZE - 1)) + size <= CHERI_CAP_SIZE) {
        flags = probe_access_flags_fast(env, vaddr, size, MMU_DATA_STORE,
                                        mmu_idx, true, &host, &block, pc);
        /* Check we got a real tagblock */
        if (block && ((flags & TLB_INVALID_MASK) == 0)) {
            txn->cpu = env_cpu(env);
            txn->flags = CHERI_TAG_TXN_VIRT;
            txn->addr = vaddr;
            txn->host = host;
            txn->lock[0] = block;
            txn->nlocks = 1;
            txn->nreal = 1;
            txn->span[0].block = block;
            txn->span[0].first = page_vaddr_to_tag_offset(vaddr);
            txn->span[0].end = txn->span[0].first + 1;
            txn->nspans = 1;
            return true;
        }
    }
    return false;
}

void cheri_tag_store_begin(CPUArchState *env, target_ulong vaddr, int size,
                           int mmu_idx, uintptr_t pc)
{
    CheriTagTxnCpu *txn_cpu = &env_cpu(env)->cheri_txn;
    cheri_debug_assert(!txn_cpu->store_open);
    /* Try the resolve_fastcap path else fall back to the slow path */
    if (!cheri_tag_txn_resolve_fastcap(env, txn_cpu, vaddr, size, mmu_idx,
                                       pc)) {
        cheri_tag_txn_resolve(&txn_cpu->store, env, vaddr, size, mmu_idx, true,
                              pc);
    }
    cheri_tag_txn_lock(&txn_cpu->store);
    txn_cpu->store_open = true;
}

void cheri_tag_store_end_clear(CPUArchState *env, target_ulong vaddr, int size,
                               int mmu_idx, uintptr_t pc)
{
    CheriTagTxnCpu *txn_cpu = &env_cpu(env)->cheri_txn;
    CheriTagTxn txn;
    bool res G_GNUC_UNUSED;
    cheri_debug_assert(txn_cpu->store_open);
    if (txn_cpu->store.flags & CHERI_TAG_TXN_FAILED) {
        /*
         * The store actually occurred, as a fault would have longjmp'd out of
         * the store op and skipped this handler.
         */
        res = cheri_tag_txn_resolve(&txn, env, vaddr, size, mmu_idx, true, pc);
        cheri_debug_assert(res);
        cheri_tag_txn_lock(&txn);
        cheri_tag_txn_clear_tags(&txn);
        cheri_tag_txn_commit(&txn);
    } else {
        cheri_tag_txn_clear_tags(&txn_cpu->store);
        cheri_tag_txn_commit(&txn_cpu->store);
    }
    txn_cpu->store_open = false;
}

void cheri_tag_store_end(CPUArchState *env)
{
    CheriTagTxnCpu *txn_cpu = &env_cpu(env)->cheri_txn;
    cheri_debug_assert(txn_cpu->store_open);
    cheri_tag_txn_commit(&txn_cpu->store);
    txn_cpu->store_open = false;
}

bool cheri_tag_sc_begin(CheriTagTxn *txn, CPUArchState *env, target_ulong vaddr,
                        int reg, bool tag, uintptr_t pc, int mmu_idx,
                        void *res_tagmem, unsigned long res_seq, int ncaps)
{
    /* Make sure we are either working on one capability, or an aligned pair */
    cheri_debug_assert(ncaps == 1 ||
                       QEMU_IS_ALIGNED(vaddr, 2 * CHERI_CAP_SIZE));
    if (tag) {
        cheri_tag_txn_resolve_cap(txn, env, vaddr, reg, mmu_idx, pc);
        if (ncaps == 2) {
            if (txn->nspans == 1) {
                /*
                 * Resolve recorded a real block.
                 * Extends the lock to the next capability in the same block
                 */
                cheri_tag_txn_add_cap(
                    txn, txn->span[0].block,
                    page_vaddr_to_tag_offset(vaddr + CHERI_CAP_SIZE));
            } else {
                cheri_tag_txn_add_lock(
                    txn, cheri_tag_block_dummy(vaddr + CHERI_CAP_SIZE), true);
            }
        }
    } else {
        /* Store without a tag is just a plain data store */
        cheri_tag_txn_resolve(txn, env, vaddr, ncaps * CHERI_CAP_SIZE, mmu_idx,
                              false, pc);
    }
    return cheri_tag_txn_lock_reserved(txn, res_tagmem, res_seq);
}

void cheri_tag_sc_end(CheriTagTxn *txn)
{
    cheri_tag_txn_commit(txn);
}

void cheri_tag_block_clear_range(CheriTagBlock *tagblk, size_t cap_idx,
                                 size_t end_idx)
{
    int nr, bits_to_clear, word;
    unsigned long mask_to_clear;

    mask_to_clear = BITMAP_FIRST_WORD_MASK(cap_idx);
    nr = end_idx - cap_idx;
    bits_to_clear = BITS_PER_LONG - (cap_idx % BITS_PER_LONG);
    word = BIT_WORD(cap_idx);
    while (nr - bits_to_clear >= 0) {
        qatomic_set(&tagblk->tag_bitmap[word],
                    qatomic_read(&tagblk->tag_bitmap[word]) & ~mask_to_clear);
        nr -= bits_to_clear;
        bits_to_clear = BITS_PER_LONG;
        mask_to_clear = ~0UL;
        word++;
    }
    if (nr) {
        mask_to_clear &= BITMAP_LAST_WORD_MASK(end_idx);
        qatomic_set(&tagblk->tag_bitmap[word],
                    qatomic_read(&tagblk->tag_bitmap[word]) & ~mask_to_clear);
    }
}

void *cheri_tag_phys_invalidate_begin(CPUArchState *env, RAMBlock *ram,
                                      ram_addr_t ram_offset, size_t *len)
{
    CheriTagTxn txn;

    cheri_tag_txn_begin_phys_write(&txn, env, ram, ram_offset, len);
    if (txn.nlocks) {
        return txn.lock[0];
    }
    return NULL;
}

void cheri_tag_phys_invalidate_end(void *tagblock)
{
    if (tagblock) {
        tagblock_unlock_write(tagblock);
    }
}

void cheri_tag_phys_clear_range(RAMBlock *ram, ram_addr_t ram_offset,
                                ram_addr_t len)
{
    size_t blk_offset, chunk;
    CheriTagBlock *tagblk;
    CheriTagTxn txn;

    if (!ram->cheri_tags) {
        return; /* No tags so nothing to do */
    }
    while (len) {
        blk_offset = ram_offset % CHERI_TAG_BLOCK_SIZE;
        chunk = MIN(len, CHERI_TAG_BLOCK_SIZE - blk_offset);
        tagblk = cheri_tag_block_at(ram_offset / CHERI_CAP_SIZE, ram);
        if (tagblk) {
            /* A tagblock exists for this range */
            cheri_tag_txn_begin_phys_write(&txn, NULL, ram, ram_offset, &chunk);
            cheri_tag_txn_commit(&txn);
        }
        ram_offset += chunk;
        len -= chunk;
    }
}

void cheri_phys_memcpy(CPUArchState *env, RAMBlock *ram, ram_addr_t ram_offset,
                       uint8_t *dst, const uint8_t *src, size_t len)
{
    CheriTagTxn txn;
    size_t towrite = len;
    while (towrite) {
        cheri_tag_txn_begin_phys_write(&txn, env, ram, ram_offset, &towrite);
        memcpy(dst, src, towrite);
        cheri_tag_txn_commit(&txn);
        src += towrite;
        dst += towrite;
        ram_offset += towrite;
        len -= towrite;
        towrite = len;
    }
}
/*
 * TODO: Basically nothing uses this physical address. Tag set probably should
 * not have to return it.
 */
#define handle_paddr_return(rw)                                                \
    do {                                                                       \
        if (ret_paddr) {                                                       \
            *ret_paddr = (vaddr & ~TARGET_PAGE_MASK) |                         \
                         (tlb_entry(env, mmu_idx, vaddr)->addr_##rw &          \
                          TARGET_PAGE_MASK);                                   \
        }                                                                      \
    } while (0)

bool cheri_tag_get(CPUArchState *env, target_ulong vaddr, int reg,
                   hwaddr *ret_paddr, int *prot, uintptr_t pc, int mmu_idx,
                   void *host_addr, void *tagmem, tag_reader_lock_t *lock)
{
    uintptr_t tagmem_flags;

    if (lock && lock->started) {
        bool ret = false;
        if (lock->tagmem) {
            ret = __tagblock_tag_get(lock->tagmem,
                                     page_vaddr_to_tag_offset(vaddr));
            lock->success &= tagblock_end_read(lock->tagmem, lock->seq);
        }
        return ret;
    }

    if (host_addr == NULL) {
        host_addr =
            probe_read_fast(env, vaddr, CHERI_CAP_SIZE, mmu_idx, pc, &tagmem);
    }
    handle_paddr_return(read);

    if (prot) {
        *prot = 0;
    }
    tagmem = cheri_tag_block_for_probed(env, vaddr, mmu_idx, false, tagmem,
                                        &tagmem_flags);
    if (prot) {
        if (tagmem_flags & TLBENTRYCAP_FLAG_CLEAR) {
            *prot |= PAGE_LC_CLEAR;
        }
        if (tagmem_flags & TLBENTRYCAP_FLAG_TRAP) {
            *prot |= PAGE_LC_TRAP;
        }
        if (tagmem_flags & TLBENTRYCAP_FLAG_TRAP_ANY) {
            *prot |= PAGE_LC_TRAP_ANY;
        }
    }

    /*
     * Squash happens in the caller, so read the tagblk even if
     * TLBENTRYCAP_FLAG_CLEAR
     */
    bool result = false;
    if (lock) {
        lock->tagmem = tagmem;
        lock->success = true;
        lock->started = true;
    }
    /* Memory without tag storage never holds a tag */
    if (tagmem) {
        if (lock) {
            lock->success = tagblock_begin_read(tagmem, &lock->seq);
        }
        result = __tagblock_tag_get(tagmem, page_vaddr_to_tag_offset(vaddr));
    }
    // XXX: Not atomic w.r.t. writes to tag memory
    qemu_maybe_log_instr_extra(
        env, "    Cap Tag Read [" TARGET_FMT_lx "/" RAM_ADDR_FMT "] -> %d\n",
        vaddr, qemu_ram_addr_from_host(host_addr), result);
    return result;
}

int cheri_tag_get_many(CPUArchState *env, target_ulong vaddr, int reg,
        hwaddr *ret_paddr, uintptr_t pc)
{

    const int mmu_idx = cpu_mmu_index(env, false);
    probe_read(env, vaddr, CAP_TAG_MANY_DATA_SIZE, mmu_idx, pc);
    handle_paddr_return(read);

    uintptr_t tagmem_flags;
    void *tagmem = cheri_tag_block_for_probed(env, vaddr, mmu_idx, false, NULL,
                                              &tagmem_flags);

    int result = (!tagmem || (tagmem_flags & TLBENTRYCAP_FLAG_CLEAR))
                     ? 0
                     : tagmem_get_tag_many(tagmem, vaddr);

    if ((result && (tagmem_flags & TLBENTRYCAP_FLAG_TRAP)) ||
        (tagmem_flags & TLBENTRYCAP_FLAG_TRAP_ANY)) {
        raise_load_tag_exception(env, vaddr, reg, pc);
    }

    return result;
}

void cheri_tag_set_many(CPUArchState *env, uint32_t tags, target_ulong vaddr,
                        int reg, hwaddr *ret_paddr, uintptr_t pc)
{
    tags &= CAP_TAG_GET_MANY_MASK;

    const int mmu_idx = cpu_mmu_index(env, false);
    store_capcause_reg(env, reg);
    /*
     * We call probe_(cap)_write rather than probe_access since the branches
     * checking access_type can be eliminated.
     */
    if (tags) {
        // Note: this probe will handle any store cap faults
        probe_cap_write(env, vaddr, CAP_TAG_MANY_DATA_SIZE, mmu_idx, pc);
    } else {
        probe_write(env, vaddr, CAP_TAG_MANY_DATA_SIZE, mmu_idx, pc);
    }
    clear_capcause_reg(env);

    handle_paddr_return(write);

    uintptr_t tagmem_flags;
    void *tagmem = cheri_tag_block_for_probed(env, vaddr, mmu_idx, true, NULL,
                                              &tagmem_flags);

    if ((tagmem_flags & TLBENTRYCAP_FLAG_CLEAR) && !tagmem) {
        return;
    }

    if (!tags && !tagmem) {
        return;
    }

    cheri_debug_assert(!(tagmem_flags & TLBENTRYCAP_FLAG_CLEAR) &&
                       "Unimplemented");

    /*
     * TLBENTRYCAP_FLAG_TRAP prevents writing non-zero tags, and should have
     * trapped in probe_cap_write().
     */
    assert(tags == 0 || !(tagmem_flags & TLBENTRYCAP_FLAG_TRAP));

    cheri_debug_assert(tagmem);

    tagmem_set_tag_many(tagmem, vaddr, tags);
}

/*
 * Some TCG operations on data need to be atomic. This is done using atomic host
 * operations, or single stepping. Tags pose a problem, as no host operations
 * can simultaneously set tags and data, so that would leave only single
 * stepping, which would not perform very well.
 * Luckily, a lock now exists on every capability sized unit of memory, so
 * atomic TCG operations are done by taking the lock and then doing a non-atomic
 * sequence. Memory without tag storage has no tag block, so a transaction
 * locks a dummy block instead. The dummy block depends only on the capability's
 * index in its page. Every virtual alias of a physical capability gets the
 * same dummy lock.
 */

CheriTagBlock dummy_tag_table[TAGS_PER_BLOCK_MAX];

CheriTagBlock *cheri_tag_block_dummy(target_ulong vaddr)
{
    return &dummy_tag_table[page_vaddr_to_tag_offset(vaddr)];
}

bool cheri_tag_get_debug(RAMBlock *ram, ram_addr_t ram_offset)
{
    /* Return zero tag for ROM, etc. */
    if (!ram->cheri_tags) {
        return false;
    }
    cheri_debug_assert(!memory_region_is_rom(ram->mr) &&
                       !memory_region_is_romd(ram->mr));

    cheri_debug_assert(QEMU_ALIGN_DOWN(ram_offset, CHERI_CAP_SIZE) ==
                       ram_offset);

    uint64_t tag = ram_offset / CHERI_CAP_SIZE;
    CheriTagBlock *tagblk = cheri_tag_block_at(tag, ram);
    if (!tagblk) {
        return false; /* No tagblock == no tags on this page. */
    }
    const size_t tagblk_index = tag % TAGS_PER_PAGE;
    bool tag_value = __tagblock_tag_get(tagblk, tagblk_index);
    return tag_value;
}

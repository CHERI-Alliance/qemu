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
 * XXX: I/O threads still exist even without MTTCG and need to have tag
 * clearing be atomic with their writes. Currently various places just write to
 * guest memory directly and then we tag clear in invalidate_and_set_dirty.
 * Well-behaved guests should not be touching memory handed off to DMA-capable
 * devices but a malicious guest could repeatedly DMA powerful capability bit
 * patterns on top of valid capabilities and try to race to read in between the
 * DMA write and the tag invalidate.
 */

#ifndef CAP_TAG_GET_MANY_SHFT
#error "Define a CAP_TAG_GET_MANY_SHFT in appropriate cheri-archspecific.h"
#endif

_Static_assert((1 << CAP_TAG_GET_MANY_SHFT) <= BITS_PER_LONG, "");

#define CAP_TAG_GET_MANY_MASK (~0UL >> (BITS_PER_LONG - \
        (1UL << CAP_TAG_GET_MANY_SHFT)))
#define CAP_TAG_MANY_DATA_SIZE (CHERI_CAP_SIZE << CAP_TAG_GET_MANY_SHFT)

static inline size_t num_tagblocks(RAMBlock* ram)
{
    uint64_t memory_size = memory_region_size(ram->mr);
    size_t result = DIV_ROUND_UP(memory_size, CHERI_CAP_SIZE * TAGS_PER_PAGE);
    if (memory_size != result * CHERI_CAP_SIZE * TAGS_PER_PAGE) {
        warn_report_once(
            "WARNING: memory region %s size %" PRIu64
            " is not a multiple of tag block size %d\r",
            memory_region_name(ram->mr),
            memory_size,
            CHERI_CAP_SIZE * TAGS_PER_PAGE);
    }
    return result;
}

static void
lock_tag_write_tag_and_release(CheriTagBlock *lock, CheriTagBlock *todo,
                               target_ulong vaddr, bool tag)
{
    if (tag) {
        __tagblock_tag_set(lock, page_vaddr_to_tag_offset(vaddr));
    } else {
        __tagblock_tag_clear(lock, page_vaddr_to_tag_offset(vaddr));
    }
    if (lock != todo) {
        tagblock_unlock_write(lock);
    }
}

#define TAGBLOCK_CHUNK_SIZE     0x4000
static __thread CheriTagBlock *tag_alloc_data;
static __thread unsigned int tag_alloc_used;

static CheriTagBlock *cheri_tag_new_tagblk(RAMBlock *ram, uint64_t tagidx)
{
    CheriTagBlock *tagblk, *old;

    assert(TAGBLOCK_CHUNK_SIZE % sizeof(CheriTagBlock) == 0);
    if (tag_alloc_data == NULL ||
        tag_alloc_used == TAGBLOCK_CHUNK_SIZE / sizeof(CheriTagBlock)) {
        tag_alloc_used = 0;
        tag_alloc_data = mmap(NULL, TAGBLOCK_CHUNK_SIZE,
                              PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
                              0, 0);
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

static inline QEMU_ALWAYS_INLINE CheriTagBlock *cheri_tag_block(size_t tag_index,
                                                                RAMBlock *ram)
{
    const size_t tagbock_index = tag_index / TAGS_PER_PAGE;
    cheri_debug_assert(ram->cheri_tags);
    if (tagbock_index >= num_tagblocks(ram)) {
        error_report("Call to access tag out of bounds");
        return NULL;
    }
    CheriTagBlock **tagmem = (CheriTagBlock **)ram->cheri_tags;
    /* pairs with qatomic_cmpxchg in cheri_tag_new_tagblk */
    return qatomic_load_acquire(&tagmem[tagbock_index]);
}

static inline QEMU_ALWAYS_INLINE void
tagmem_set_tag(CheriTagBlock *tagmem, target_ulong vaddr,
               tag_writer_lock_t *lock, bool lock_only)
{
    if (lock) {
        *lock = tagmem;
        tagblock_lock_write(tagmem);
        if (!lock_only) {
            __tagblock_tag_set(tagmem, page_vaddr_to_tag_offset(vaddr));
        }
    } else {
        __tagblock_tag_set(tagmem, page_vaddr_to_tag_offset(vaddr));
    }
}

static inline QEMU_ALWAYS_INLINE void
tagmem_clear_tag(CheriTagBlock *tagmem, target_ulong vaddr,
                 tag_writer_lock_t *lock, tag_writer_lock_t done,
                 bool lock_only)
{
    if (lock) {
        *lock = tagmem;
        if (tagmem != done) {
            tagblock_lock_write(tagmem);
        }
        if (!lock_only) {
            __tagblock_tag_clear(tagmem, page_vaddr_to_tag_offset(vaddr));
        }
    } else {
        __tagblock_tag_clear(tagmem, page_vaddr_to_tag_offset(vaddr));
    }
}

static inline QEMU_ALWAYS_INLINE void
tagblock_clear_locktag_with_lock_and_release(CheriTagBlock *block,
                                             target_ulong vaddr)
{
    tagblock_lock_write(block);
    __tagblock_tag_clear(block, page_vaddr_to_tag_offset(vaddr));
    tagblock_unlock_write(block);
}

static inline QEMU_ALWAYS_INLINE int
tagmem_get_tag_many(CheriTagBlock *tagmem, target_ulong vaddr)
{
    unsigned long res;
    unsigned long index = page_vaddr_to_tag_offset(vaddr);
    while (1) {
        unsigned long seq;
        if (!tagblock_begin_read(tagmem, &seq)) {
            cpu_relax();
            continue;
        }
        res = qatomic_read(tagmem->tag_bitmap + BIT_WORD(index));
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

    assert(TARGET_PAGE_SIZE == CHERI_TAG_BLOCK_SIZE);
    size_t cheri_ntagblks = num_tagblocks(mr->ram_block);
    mr->ram_block->cheri_tags =
        g_malloc0(cheri_ntagblks * sizeof(CheriTagBlock *));
    if (mr->ram_block->cheri_tags == NULL) {
        error_report("%s: Can't allocated tag memory", __func__);
        exit(-1);
    }
}

void *cheri_tagmem_for_addr(CPUArchState *env, target_ulong vaddr,
                            RAMBlock *ram, ram_addr_t ram_offset, size_t size,
                            int *prot, bool tag_write)
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
        return ALL_ZERO_TAGBLK;
    }

    uint64_t tag = ram_offset / CHERI_CAP_SIZE;
#ifndef TARGET_AARCH64
    // AArch64 seems to use different sizes. Might be worth looking into.
    cheri_debug_assert(ram_offset % TARGET_PAGE_SIZE == 0 &&
                       "Unexpected offset");
    cheri_debug_assert(size == TARGET_PAGE_SIZE && "Unexpected size");
#endif
    CheriTagBlock *tagblk = cheri_tag_block(tag, ram);

    if (!tagblk) {
        cheri_tag_new_tagblk(ram, tag);
        CPUState *cpu = env_cpu(env);
        /*
         * A vaddr-based shootdown is insufficient as multiple mappings may
         * exist. Short of an inverted table, a complete shootdown is required.
         */
        tlb_flush_all_cpus_synced(cpu);
        /*
         * An un-synced flush the current cpu is required as we want to complete
         * this instruction and THEN exit.
         */
        tlb_flush(cpu);
        tagblk = cheri_tag_block(tag, ram);
        cheri_debug_assert(tagblk);
    }

    if (tagblk != NULL) {
        return tagblk;
    }

    if (!(*prot & PAGE_SC_CLEAR)) {
        // Add in a (fake) SC_TRAP to prompt a TLB refill if a tag is stored
        // to this location. See the comment around TLBENTRYCAP_INVALID_WRITE_*.
        *prot |= PAGE_SC_TRAP;
    }

    return ALL_ZERO_TAGBLK;
}

static inline void *get_tagmem_from_iotlb_entry(CPUArchState *env,
                                                target_ulong vaddr, int mmu_idx,
                                                bool isWrite,
                                                uintptr_t *flags_out)
{
    /* XXXAR: see mte_helper.c */
    /*
     * Find the iotlbentry for ptr.  This *must* be present in the TLB
     * because we just found the mapping.
     * TODO: Perhaps there should be a cputlb helper that returns a
     * matching tlb entry + iotlb entry.
     */
#ifdef CONFIG_DEBUG_TCG
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, vaddr);
    g_assert(tlb_hit(isWrite ? tlb_addr_write(entry) : entry->addr_read, vaddr));
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

typedef struct TagOffset {
    target_ulong value;
} TagOffset;

static inline QEMU_ALWAYS_INLINE TagOffset addr_to_tag_offset(target_ulong addr)
{
    return (struct TagOffset){.value = addr / CHERI_CAP_SIZE};
}

static inline QEMU_ALWAYS_INLINE target_ulong
tag_offset_to_addr(TagOffset offset)
{
    return offset.value * CHERI_CAP_SIZE;
}

static void *cheri_tag_invalidate_one(CPUArchState *env, target_ulong vaddr,
                                      int32_t size, uintptr_t pc, int mmu_idx,
                                      tag_writer_lock_t *lock,
                                      tag_writer_lock_t done, bool lock_only);
static void invalidate_from_locktag(CheriTagBlock *lock, CheriTagBlock *todo,
                                    CPUArchState *env,
                                    target_ulong vaddr, uintptr_t pc,
                                    int mmu_idx)
{
    if (lock != TAG_LOCK_NONE) {
        if (lock == TAG_LOCK_ERROR) {
            return;
        }
        lock_tag_write_tag_and_release(lock,  todo, vaddr, 0);
    }
}

void *cheri_tag_invalidate_aligned_impl(CPUArchState *env, target_ulong vaddr,
                                        uintptr_t pc, int mmu_idx,
                                        tag_writer_lock_t *lock, bool lock_only)
{
    cheri_debug_assert(QEMU_IS_ALIGNED(vaddr, CHERI_CAP_SIZE));
    if (lock && *lock) {
        invalidate_from_locktag(*lock, NULL, env, vaddr, pc, mmu_idx);
        return NULL;
    }
    return cheri_tag_invalidate_one(env, vaddr, CHERI_CAP_SIZE, pc, mmu_idx,
                                    lock, NULL, lock_only);
}

void cheri_tag_invalidate_impl(CPUArchState *env, target_ulong vaddr,
                               int32_t size, uintptr_t pc, int mmu_idx,
                               tag_writer_lock_t *first,
                               tag_writer_lock_t *second, bool lock_only)
{
    cheri_debug_assert(size > 0);

    if (first && *first) {
        vaddr = (vaddr / CHERI_CAP_SIZE) * CHERI_CAP_SIZE;
        invalidate_from_locktag(*first, second ? *second : NULL, env, vaddr,
                                pc, mmu_idx);
        if (second && *second) {
            invalidate_from_locktag(*second, NULL, env,
                    vaddr + CHERI_CAP_SIZE, pc, mmu_idx);
        }
        return;
    }

    target_ulong first_addr = vaddr;
    target_ulong last_addr = (vaddr + size - 1);
    TagOffset tag_start = addr_to_tag_offset(first_addr);
    TagOffset tag_end = addr_to_tag_offset(last_addr);
    if (likely(tag_start.value == tag_end.value)) {
        // Common case, only one tag (i.e. an aligned store)
        cheri_tag_invalidate_one(env, vaddr, size, pc, mmu_idx, first, NULL,
                                 lock_only);
        return;
    }

    /*
     * Unaligned store -> can cross a capability alignment boundary and
     * therefore invalidate two tags. It can also cross pages.
     * It can also wrap the address space, so the offset difference should be
     * computed modulo the number of capability sized granules.
     */
    size_t ntags = ((tag_end.value - tag_start.value) &
            ((target_ulong)-1 / CHERI_CAP_SIZE)) + 1;
    assert(ntags == 2 && "Should invalidate at most two tag bits here");
    // Note: we always do a new page lookup here since unaligned stores
    // should be rare and the probing should be fast if it's in the QEMU TLB
#if defined(TARGET_ALIGNED_ONLY)
#if defined(CHERI_UNALIGNED)
    if (unlikely((first_addr & TARGET_PAGE_MASK) !=
                 (last_addr & TARGET_PAGE_MASK))) {
        warn_report("Got unaligned %d-byte store across page "
                    "boundary at 0x" TARGET_FMT_lx "\r\n",
                    size, vaddr);
    }
#else
    if (!lock_only) {
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
    tag_writer_lock_t done = NULL;
    target_ulong addr = tag_offset_to_addr(tag_start);
    for (size_t i = 0; i < ntags; i++, addr += CHERI_CAP_SIZE) {
        cheri_tag_invalidate_one(env, addr, CHERI_CAP_SIZE, pc, mmu_idx,
                                 first, done, lock_only);
        if (first && (*first)) {
            done = *first;
        }
        first = second;
    }
}

static void *cheri_tag_invalidate_one(CPUArchState *env, target_ulong vaddr,
                                      int32_t size, uintptr_t pc, int mmu_idx,
                                      tag_writer_lock_t *lock,
                                      tag_writer_lock_t done, bool lock_only)
{
    cheri_debug_assert(size > 0);
    if (lock) {
        *lock = TAG_LOCK_NONE;
    }

    void *tagmem = NULL;

    /*
     * We use the flags version to suppress exceptions if we only want the
     * lock. This is required as the data access might throw an exception with a
     * higher priority. It should be the case that the data access will throw an
     * MMU exception as well, but if it doesn't, I am returning another special
     * lock value which can be tested later
     */
    void *host_addr;
    int access_flags = probe_access_flags_fast(env, vaddr, size,
                                               MMU_DATA_STORE, mmu_idx,
                                               lock_only, &host_addr,
                                               &tagmem, pc);
    if (likely(tagmem)) {
        tagmem_clear_tag(tagmem, vaddr, lock, done, lock_only);
        return host_addr;
    }

    if (access_flags & TLB_INVALID_MASK) {
        if (lock) {
            *lock = TAG_LOCK_ERROR;
        }
        return host_addr;
    }

    // Only RAM and ROM regions are backed by host addresses so if
    // probe_write() returns NULL we know that we can't write the tagmem.
    if (unlikely(!host_addr)) {
        return host_addr;
    }

    uintptr_t tagmem_flags;
    tagmem =
        get_tagmem_from_iotlb_entry(env, vaddr, mmu_idx, true, &tagmem_flags);

    if (tagmem == ALL_ZERO_TAGBLK) {
        // All tags for this page are zero -> no need to invalidate. We also
        // couldn't invalidate if we wanted to since ALL_ZERO_TAGBLK is not a
        // valid pointer but a magic constant.
        return host_addr;
    }

    cheri_debug_assert(!(tagmem_flags & TLBENTRYCAP_FLAG_CLEAR) &&
                       "Unimplemented");

    // We cached the tagblock address in the iotlb -> find find the tag
    // bit: The iotlb->tagmem pointer always points to the tag memory
    // for the start of the page so we can simply add the index for the
    // page offset.
    tagmem_clear_tag(tagmem, vaddr, lock, done, lock_only);
    return host_addr;
}



void cheri_tag_phys_invalidate_external(RAMBlock *ram,
                               ram_addr_t ram_offset, ram_addr_t len)
{
    cheri_tag_phys_invalidate(NULL,ram,ram_offset,len,NULL);
}


void cheri_tag_phys_invalidate(CPUArchState *env, RAMBlock *ram,
                               ram_addr_t ram_offset, ram_addr_t len,
                               const target_ulong *vaddr)
{
    // Ignore tag clearing requests for ROM, etc.
    if (!ram->cheri_tags) {
        return;
    }
    cheri_debug_assert(!memory_region_is_rom(ram->mr) &&
                       !memory_region_is_romd(ram->mr));

    ram_addr_t endaddr = (uint64_t)(ram_offset + len);
    ram_addr_t startaddr = QEMU_ALIGN_DOWN(ram_offset, CHERI_CAP_SIZE);

    for(ram_addr_t addr = startaddr; addr < endaddr; addr += CHERI_CAP_SIZE) {
        uint64_t tag = addr / CHERI_CAP_SIZE;
        CheriTagBlock *tagblk = cheri_tag_block(tag, ram);
        if (tagblk != NULL) {
            const size_t tagblk_index = tag % TAGS_PER_PAGE;
            if (unlikely(env && qemu_log_instr_enabled(env))) {
                bool old_tag = __tagblock_tag_get(tagblk, tagblk_index);
                if (vaddr) {
                    target_ulong write_vaddr =
                        QEMU_ALIGN_DOWN(*vaddr, CHERI_CAP_SIZE) +
                        (addr - startaddr);
                    qemu_log_instr_extra(env,
                                         "    Cap Tag Write [" TARGET_FMT_lx
                                         "/" RAM_ADDR_FMT "] %d -> 0\n",
                                         write_vaddr, addr, old_tag);
                } else {
                    qemu_log_instr_extra(
                        env,
                        "    Cap Tag ramaddr Write [" RAM_ADDR_FMT
                        "] %d -> 0\n",
                        addr, old_tag);
                }
            }

            tagblock_clear_locktag_with_lock_and_release(tagblk,
                                                         addr | (0x1UL << 63));
        }
    }
}

void *cheri_tag_phys_invalidate_begin(CPUArchState *env, RAMBlock *ram,
                                      ram_addr_t ram_offset, size_t *len)
{
    CheriTagBlock *tagblk;
    size_t end_idx;
    size_t blk_offset;
    size_t start_idx, cap_idx;
    int i, nr, bits_to_clear, word;
    unsigned long mask_to_clear;
    bool old_tag;
    ram_addr_t ram_addr;

    /*
     * The ram offset is the offset into the ramblock. We need to convert this
     * into capability indices
     */
    start_idx = ram_offset / CHERI_CAP_SIZE;
    blk_offset = ram_offset % CHERI_TAG_BLOCK_SIZE;

    /* Limit the operation to a single tag block */
    *len = *len + blk_offset < CHERI_TAG_BLOCK_SIZE ? *len :
        CHERI_TAG_BLOCK_SIZE - blk_offset;

    end_idx = DIV_ROUND_UP(blk_offset + *len, CHERI_CAP_SIZE);
    if (ram->cheri_tags == NULL) {
        /* Nothing to lock */
        return NULL;
    }

    tagblk = cheri_tag_block(start_idx, ram);
    if (!tagblk) {
        /* Attempt to allocate a new tagblock */
        tagblk = cheri_tag_new_tagblk(ram, start_idx);
    }
    tagblock_lock_write(tagblk);
    if (*len == 0) {
        return tagblk;
    }
    cap_idx = blk_offset / CHERI_CAP_SIZE;
    mask_to_clear = BITMAP_FIRST_WORD_MASK(cap_idx);
    nr = end_idx - cap_idx;
    bits_to_clear = BITS_PER_LONG - (cap_idx % BITS_PER_LONG);
    word = BIT_WORD(cap_idx);
    if (unlikely(env && qemu_log_instr_enabled(env))) {
        /* Before we clear the bits we need to log any that are set */
        for (ram_addr = QEMU_ALIGN_DOWN(ram_offset, CHERI_CAP_SIZE),
                i = cap_idx; i < end_idx; i++, ram_addr += CHERI_CAP_SIZE) {
            old_tag = __tagblock_tag_get(tagblk, i);
            if (old_tag) {
                qemu_log_instr_extra(
                    env,
                    "    Cap Tag ramaddr Write [" RAM_ADDR_FMT
                    "] %d -> 0\n",
                    ram_addr, old_tag);
            }
        }
    }
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
    return tagblk;
}

void cheri_tag_phys_invalidate_end(void *tagblock)
{
    if (tagblock) {
        tagblock_unlock_write(tagblock);
    }
}

void cheri_phys_memcpy(CPUArchState *env, RAMBlock *ram,
                       ram_addr_t ram_offset, uint8_t *dst,
                       const uint8_t *src, size_t len)
{
    void *tagblk;
    size_t towrite = len;
    while (towrite) {
        tagblk = cheri_tag_phys_invalidate_begin(env, ram, ram_offset,
                                                 &towrite);
        memcpy(dst, src, towrite);
        cheri_tag_phys_invalidate_end(tagblk);
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

#ifdef TARGET_MIPS
#define store_capcause_reg(env, reg) cpu_mips_store_capcause_reg(env, reg)
#define clear_capcause_reg(env) cpu_mips_clear_capcause_reg(env)
#else
#define store_capcause_reg(env, reg)
#define clear_capcause_reg(env)
#endif

void *cheri_tag_set_impl(CPUArchState *env, target_ulong vaddr, int reg,
                         hwaddr *ret_paddr, uintptr_t pc, int mmu_idx,
                         tag_writer_lock_t *lock, bool lock_only)
{

    if (lock && *lock) {
        if (*lock != TAG_LOCK_NONE) {
            lock_tag_write_tag_and_release(*lock, NULL, vaddr, 1);
        }
        return NULL;
    }

    void *tagmem;

    /*
     * This attempt to resolve a virtual address may cause both a data store
     * TLB fault (entry missing or D bit clear) and a capability store TLB
     * fault (SC bit set).
     */
    store_capcause_reg(env, reg);
    // Note: this probe will handle any store cap faults
    void *host_addr = probe_cap_write_fast(env, vaddr, CHERI_CAP_SIZE,
                                           mmu_idx, pc, &tagmem);
    clear_capcause_reg(env);

    handle_paddr_return(write);

    if (lock) {
        *lock = TAG_LOCK_NONE;
    }

    if (unlikely(!host_addr)) {
        return NULL;
    }

    if (likely(tagmem)) {
        tagmem_set_tag(tagmem, vaddr, lock, lock_only);
        return host_addr;
    }

    uintptr_t tagmem_flags;
    tagmem = get_tagmem_from_iotlb_entry(env, vaddr, mmu_idx,
                                               /*write=*/true, &tagmem_flags);

    /* Clear + ALL_ZERO_TAGBLK means no tags can be stored here. */
    if ((tagmem_flags & TLBENTRYCAP_FLAG_CLEAR) &&
        (tagmem == ALL_ZERO_TAGBLK)) {
        return host_addr;
    }

    cheri_debug_assert(!(tagmem_flags & TLBENTRYCAP_FLAG_CLEAR) &&
                       "Unimplemented");

    cheri_debug_assert(!(tagmem_flags & TLBENTRYCAP_FLAG_TRAP));

    /*
     * probe_cap_write() should have ensured there was a tagmem for this
     * location. A NULL ram should have been indicated via
     * TLBENTRYCAPFLAG_CLEAR or TLBENTRYCAPFLAG_TRAP.
     */
    cheri_debug_assert(tagmem != ALL_ZERO_TAGBLK);

    qemu_maybe_log_instr_extra(
        env, "    Cap Tag Write [" TARGET_FMT_lx "/" RAM_ADDR_FMT "] %d -> 1\n",
        vaddr, qemu_ram_addr_from_host(host_addr),
        __tagblock_tag_get(tagmem, page_vaddr_to_tag_offset(vaddr)));

    tagmem_set_tag(tagmem, vaddr, lock, lock_only);
    return host_addr;
}

bool cheri_tag_get(CPUArchState *env, target_ulong vaddr, int reg,
                   hwaddr *ret_paddr, int *prot, uintptr_t pc, int mmu_idx,
                   void *host_addr, void *tagmem, tag_reader_lock_t *lock)
{
    if (lock && lock->tagmem) {
        bool ret = false;
        if (lock->tagmem != ALL_ZERO_TAGBLK) {
            ret = __tagblock_tag_get(lock->tagmem,
                                     page_vaddr_to_tag_offset(vaddr));
            lock->success &= tagblock_end_read(lock->tagmem, lock->seq);
        }
        return ret;
    }

    if (host_addr == NULL) {
        host_addr = probe_read_fast(env, vaddr, CHERI_CAP_SIZE, mmu_idx, pc,
                                    &tagmem);
    }
    handle_paddr_return(read);

    if (prot) {
        *prot = 0;
    }
    if (unlikely(!tagmem)) {
        uintptr_t tagmem_flags;
        tagmem = get_tagmem_from_iotlb_entry(env, vaddr, mmu_idx,
                                             /*write=*/false, &tagmem_flags);

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
    }

    /*
     * Squash happens in the caller, so read the tagblk even if
     * TLBENTRYCAP_FLAG_CLEAR
     */
    bool result = false;
    if (lock) {
        lock->tagmem = tagmem;
        lock->success = true;
    }
    /* Memory without tag storage never holds a tag */
    if (tagmem && tagmem != ALL_ZERO_TAGBLK) {
        if (lock) {
            lock->success = tagblock_begin_read(tagmem, &lock->seq);
        }
        result = __tagblock_tag_get(tagmem,
                                    page_vaddr_to_tag_offset(vaddr));
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
    void *tagmem = get_tagmem_from_iotlb_entry(env, vaddr, mmu_idx,
                                               /*write=*/false, &tagmem_flags);

    int result =
        ((tagmem == ALL_ZERO_TAGBLK) ||
         (tagmem_flags & TLBENTRYCAP_FLAG_CLEAR))
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
    void *tagmem =
        get_tagmem_from_iotlb_entry(env, vaddr, mmu_idx, true, &tagmem_flags);

    if ((tagmem_flags & TLBENTRYCAP_FLAG_CLEAR) &&
        (tagmem == ALL_ZERO_TAGBLK)) {
        return;
    }

    if (!tags && tagmem == ALL_ZERO_TAGBLK) {
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

void cheri_tag_writer_lock_release_impl(tag_writer_lock_t lock)
{
    tagblock_unlock_write(lock);
}

void cheri_tag_free_lock(tag_lock_t lock)
{
    if (!lock || lock == TAG_LOCK_NONE || lock == TAG_LOCK_ERROR) {
        return;
    }
    tagblock_unlock_write(lock);
}

/*
 * Some TCG operations on data need to be atomic. This is done using atomic host
 * operations, or single stepping. Tags pose a problem, as no host operations
 * can simultaneously set tags and data, so that would leave only single
 * stepping, which would not perform very well.
 * Luckily, a lock now exists on every capability sized unit of memory, so
 * atomic TCG operations are done by taking the lock and then doing a non-atomic
 * sequence. Because it is unknown at translation time whether a location will
 * have a tag (and therefore a lock), we cannot know whether we can rely
 * on the tags lock or not.
 * Therefore, we assume every location is tagged. If it is not, we asign it a
 * dummy lock (that will always have a zero tag).
 * We have a few of these to avoid false sharing.
 */

#define DUMMY_LOCK_BITS 16

CheriTagBlock dummy_tag_table[1 << DUMMY_LOCK_BITS];
/*
 * As the address we are passed is virtual but our locks need to be on physical
 * addresses only use the low order bits i.e. page offset for the hash
 * This will ensure that accesses to the physical address will use the same hash
 * It will also increase the likilihood of false sharing but will still be
 * safe
 */
static target_ulong hash_vaddr(target_ulong vaddr)
{
    vaddr &= ~TARGET_PAGE_MASK;
    size_t shft = sizeof(target_ulong);
    while (shft > DUMMY_LOCK_BITS) {
        shft /= 2;
        vaddr ^= vaddr >> shft;
    }
    return vaddr & ((1 << DUMMY_LOCK_BITS) - 1);
}

static CheriTagBlock *get_dummy(target_ulong vaddr)
{
    return &dummy_tag_table[hash_vaddr(vaddr)];
}

void get_dummy_locks(target_ulong vaddr, tag_writer_lock_t *low,
                     tag_writer_lock_t *high)
{
    target_ulong tag_addr1 = vaddr / CHERI_CAP_SIZE;
    target_ulong tag_addr2 = tag_addr1 + 1;
    tag_writer_lock_t low_dummy = NULL;
    tag_writer_lock_t high_dummy = NULL;

    if (*low == TAG_LOCK_NONE) {
        low_dummy = get_dummy(tag_addr1);
    }

    if (*high == TAG_LOCK_NONE) {
        high_dummy = get_dummy(tag_addr2);
        if (high_dummy == low_dummy) {
            high_dummy = NULL;
        }
    }

    if (low_dummy && high_dummy && high_dummy < low_dummy) {
        /* Keep lock order consistent. */
        tag_writer_lock_t tmp = high_dummy;
        high_dummy = low_dummy;
        low_dummy = tmp;
    }

    if (low_dummy) {
        tagblock_lock_write(low_dummy);
        *low = low_dummy;
    }

    if (high_dummy) {
        tagblock_lock_write(high_dummy);
        *high = high_dummy;
    }
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
    CheriTagBlock *tagblk = cheri_tag_block(tag, ram);
    cheri_debug_assert(tagblk);
    const size_t tagblk_index = tag % TAGS_PER_PAGE;
    bool tag_value = __tagblock_tag_get(tagblk, tagblk_index);
    return tag_value;
}

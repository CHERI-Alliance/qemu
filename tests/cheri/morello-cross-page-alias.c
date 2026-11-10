/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Exercises page-granular tag-lock deadlocks and exception unwind across
 * virtual-memory page boundaries on Morello:
 * - Ring-buffer alias [page_a, page_a]: an unaligned STR or STP straddling
 *   the boundary touches granule 255 and granule 0 of the same physical page,
 *   which self-deadlocks if per-page locks are not deduplicated.
 * - Faulting boundary [page_a, unmapped]: an unaligned STR faults before
 *   writing (preserving page_a's tag), whereas an STP writes the first half
 *   (clearing page_a's tag) before faulting; both must release page_a's tag
 *   lock so subsequent capability loads and stores on page_a succeed.
 * - Reversed physical aliases [page_a, page_b] vs [page_b, page_a] on two
 *   CPUs: concurrent cross-page unaligned stores AB-BA deadlock unless page
 *   locks are acquired in canonical physical/host order.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 ring-buffer STR did not clear both tags, 2
 * ring-buffer STP did not clear both tags, 3 faulting unaligned STR failed or
 * clobbered page_a's tag, 4 faulting STP failed or left page_a's tag set, 5
 * page_a unusable after fault, 10 unexpected exception.
 */
#include "morello.h"

#define VA_RING   0x40200000UL
#define VA_AB     0x40202000UL
#define VA_BA     0x40204000UL
#define VA_FAULT  0x40206000UL
#define SMP_ITERS 5000

static uint64_t l1[512] ALIGNED(PAGE_SIZE);
static uint64_t l2[512] ALIGNED(PAGE_SIZE);
static uint64_t l3[512] ALIGNED(PAGE_SIZE);
static uint8_t page_a[PAGE_SIZE] ALIGNED(PAGE_SIZE);
static uint8_t page_b[PAGE_SIZE] ALIGNED(PAGE_SIZE);

static uint32_t cpu1_ready;
static uint32_t smp_go;
static uint32_t cpu1_done;

static void hammer_cross_page_boundary(uint8_t *base, uint64_t scalar_val)
{
    uint8_t *last_granule = base + 0xff0;
    uint8_t *next_granule = base + 0x1000;

    for (int i = 0; i < SMP_ITERS; i++) {
        morello_store_ddc_addr(last_granule, (uintptr_t)last_granule);
        morello_store_ddc_addr(next_granule, (uintptr_t)last_granule);
        morello_store_u64(last_granule + 12, scalar_val);
        morello_store_pair_u64(last_granule + 8, scalar_val, scalar_val);
        (void)morello_load_cap(last_granule);
    }
}

void secondary_main(unsigned long cpu_id)
{
    (void)cpu_id;
    morello_enable_mmu(l1);

    atomic_store_release(&cpu1_ready, 1);
    while (!atomic_load_acquire(&smp_go)) {
    }

    hammer_cross_page_boundary((uint8_t *)VA_BA, 0x55667788ULL);
    atomic_store_release(&cpu1_done, 1);
}

int test_main(void)
{
    uint64_t pte_a = (uintptr_t)page_a | MORELLO_PTE_ATTRS | 3;
    uint64_t pte_b = (uintptr_t)page_b | MORELLO_PTE_ATTRS | 3;

    l1[1] = (uintptr_t)l2 | 3;
    l2[0] = 0x40000000ULL | MORELLO_PTE_ATTRS | 1;
    l2[1] = (uintptr_t)l3 | 3;

    /* L3[0..1]: [page_a, page_a] ring-buffer alias at VA_RING */
    l3[0] = pte_a;
    l3[1] = pte_a;
    /* L3[2..3]: [page_a, page_b] at VA_AB */
    l3[2] = pte_a;
    l3[3] = pte_b;
    /* L3[4..5]: [page_b, page_a] at VA_BA */
    l3[4] = pte_b;
    l3[5] = pte_a;
    /* L3[6..7]: [page_a, unmapped] at VA_FAULT */
    l3[6] = pte_a;
    l3[7] = 0;

    morello_enable_mmu(l1);

    uint8_t *ring_last = (uint8_t *)(VA_RING + 0xff0);
    uint8_t *ring_next = (uint8_t *)(VA_RING + 0x1000);
    uint64_t pat = 0x11223344ULL;

    /* Ring-buffer unaligned STR across [page_a + 0xffc .. page_a + 0x003]. */
    morello_store_ddc_addr(ring_last, (uintptr_t)ring_last);
    morello_store_ddc_addr(ring_next, (uintptr_t)ring_last);
    morello_store_u64(ring_last + 12, pat);
    check(!morello_load_cap(ring_last).tag, 1);
    check(!morello_load_cap(ring_next).tag, 1);

    /* Ring-buffer STP across [page_a + 0xff8 .. page_a + 0x007]. */
    morello_store_ddc_addr(ring_last, (uintptr_t)ring_last);
    morello_store_ddc_addr(ring_next, (uintptr_t)ring_last);
    morello_store_pair_u64(ring_last + 8, pat, pat);
    check(!morello_load_cap(ring_last).tag, 2);
    check(!morello_load_cap(ring_next).tag, 2);

    /*
     * Faulting unaligned STR at VA_FAULT + 0xffc: faults on the unmapped
     * second page before writing any bytes, so page_a + 0xff0 keeps its tag
     * and page_a's lock must be released on exception unwind.
     */
    uint8_t *fault_last = (uint8_t *)(VA_FAULT + 0xff0);
    morello_store_ddc_addr(fault_last, (uintptr_t)ring_last);
    morello_fault_count = 0;
    morello_store_u64(fault_last + 12, pat);
    check(morello_fault_count == 1, 3);
    check(morello_load_cap(fault_last).tag, 3);

    /*
     * Faulting STP at VA_FAULT + 0xff8: writes the first 8 bytes on page_a
     * (clearing the tag) and faults on the second 8 bytes; page_a's lock must
     * still be released so subsequent capability ops do not hang.
     */
    morello_fault_count = 0;
    morello_store_pair_u64(fault_last + 8, pat, pat);
    check(morello_fault_count == 1, 4);
    check(!morello_load_cap(fault_last).tag, 4);
    morello_store_ddc_addr(fault_last, (uintptr_t)ring_last);
    check(morello_load_cap(fault_last).tag, 5);

    /* Wake CPU 1 for concurrent [page_a, page_b] vs [page_b, page_a] test. */
    if (morello_cpu_on(1) != 0) {
        return 0;
    }
    while (!atomic_load_acquire(&cpu1_ready)) {
    }
    atomic_store_release(&smp_go, 1);

    hammer_cross_page_boundary((uint8_t *)VA_AB, pat);

    while (!atomic_load_acquire(&cpu1_done)) {
    }
    return 0;
}

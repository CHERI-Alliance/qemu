/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A SWP of an untagged capability to a read-only page with SC=0 must take the
 * permission fault before it touches the tag. The handler makes the page
 * writable (keeping SC=0) and the retried untagged SWP must succeed without
 * taking a capability-store page fault, returning the old tagged capability
 * and leaving the stored value untagged in memory.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 old value untagged, 2 old value wrong, 3 memory
 * still tagged, 4 memory value wrong, 5 fault count not 1, 10 unexpected
 * exception.
 */
#include "morello.h"

#define VA_RO 0x40200000UL
#define VA_RW 0x40201000UL

static uint64_t l1[512] ALIGNED(PAGE_SIZE);
static uint64_t l2[512] ALIGNED(PAGE_SIZE);
static uint64_t l3[512] ALIGNED(PAGE_SIZE);
static uint8_t data_page[PAGE_SIZE] ALIGNED(PAGE_SIZE);

extern const char swap_insn[];
static volatile uint64_t fault_count;

bool on_sync_exception(uint64_t esr, uint64_t elr)
{
    uint64_t ec = (esr >> 26) & 0x3f;
    check(ec == 0x25, 10);
    check(elr == (uintptr_t)swap_insn, 10);
    fault_count++;
    check(fault_count == 1, 10);

    l3[0] &= ~MORELLO_PTE_AP_RO;
    asm volatile(
        "dsb  sy\n\t"
        "tlbi vmalle1\n\t"
        "dsb  sy\n\t"
        "isb\n\t"
        ::: "memory");
    return false; /* retry the faulting SWP instruction */
}

static struct cap_info swp_untagged_cap(void *addr, uintptr_t new_val)
{
    register uintptr_t x1 asm("x1") = new_val;
    register uintptr_t x3 asm("x3") = 0;
    register void *x6 asm("x6") = addr;
    register uintptr_t tag asm("x7");
    asm volatile(
        ".globl swap_insn\n\t"
        "swap_insn:\n\t"
        "SWP_CAP 1, 3, 6\n\t"
        "GCTAG   7, 3\n\t"
        : "+r"(x3), "=r"(tag)
        : "r"(x1), "r"(x6)
        : "memory");
    return (struct cap_info){ .addr = x3, .tag = (tag != 0) };
}

int test_main(void)
{
    l1[1] = (uintptr_t)l2 | 3;
    l2[0] = 0x40000000ULL | MORELLO_PTE_ATTRS | 1;
    l2[1] = (uintptr_t)l3 | 3;
    l3[0] = (uintptr_t)data_page | MORELLO_PTE_ATTRS_NO_SC |
            MORELLO_PTE_AP_RO | 3;
    l3[1] = (uintptr_t)data_page | MORELLO_PTE_ATTRS | 3;
    morello_enable_mmu(l1);

    void *rw_ptr = (void *)VA_RW;
    void *ro_ptr = (void *)VA_RO;
    morello_store_ddc_addr(rw_ptr, 0);

    uintptr_t untagged_new = 0x1234;
    struct cap_info old_cap = swp_untagged_cap(ro_ptr, untagged_new);

    check(old_cap.tag, 1);
    check(old_cap.addr == 0, 2);
    check(!morello_load_cap(rw_ptr).tag, 3);

    volatile uint64_t *rw_words = (volatile uint64_t *)rw_ptr;
    check(rw_words[0] == untagged_new && rw_words[1] == 0, 4);
    check(fault_count == 1, 5);
    return 0;
}

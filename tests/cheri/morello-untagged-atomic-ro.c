/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Untagged capability atomic RMW instructions on Morello must still perform a
 * faulting data-write probe before reading memory:
 * - A failed untagged STXR to a read-only page (where the in-memory value no
 *   longer matches the load-exclusive value) must raise a write permission
 *   Data Abort rather than returning status 1 without faulting.
 * - An untagged SWP to a read-only page that also traps capability loads
 *   (LC=11) must report the higher-priority store permission fault (WnR=1,
 *   DFSC=0x0f) rather than a load capability fault (WnR=0, DFSC=0x28), and
 *   must leave the existing tagged capability in memory untouched.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 failed untagged STXR on RO page did not fault, 2
 * STXR fault syndrome wrong, 3 untagged SWP on RO+LC_TRAP page did not fault,
 * 4 SWP fault syndrome had wrong priority, 5 memory clobbered, 10 unexpected
 * exception.
 */
#include "morello.h"

#define VA_RW    0x40200000UL
#define VA_RO    0x40201000UL
#define VA_RO_LC 0x40202000UL

static uint64_t l1[512] ALIGNED(PAGE_SIZE);
static uint64_t l2[512] ALIGNED(PAGE_SIZE);
static uint64_t l3[512] ALIGNED(PAGE_SIZE);
static uint8_t data_page[PAGE_SIZE] ALIGNED(PAGE_SIZE);

extern const char stxr_ro_insn[];
extern const char swp_ro_lc_insn[];
static volatile uint64_t fault_count;
static volatile uint64_t last_esr;
static volatile uint64_t last_elr;

bool on_sync_exception(uint64_t esr, uint64_t elr)
{
    uint64_t ec = (esr >> 26) & 0x3f;
    check(ec == 0x25, 10);
    fault_count++;
    last_esr = esr;
    last_elr = elr;
    return true;
}

int test_main(void)
{
    l1[1] = (uintptr_t)l2 | 3;
    l2[0] = 0x40000000ULL | MORELLO_PTE_ATTRS | 1;
    l2[1] = (uintptr_t)l3 | 3;
    l3[0] = (uintptr_t)data_page | MORELLO_PTE_ATTRS | 3;
    l3[1] = (uintptr_t)data_page | MORELLO_PTE_ATTRS_NO_SC |
            MORELLO_PTE_AP_RO | 3;
    l3[2] = (uintptr_t)data_page | MORELLO_PTE_ATTRS_LC_TRAP |
            MORELLO_PTE_AP_RO | 3;
    morello_enable_mmu(l1);

    void *rw_ptr = (void *)VA_RW;
    void *ro_ptr = (void *)VA_RO;
    void *ro_lc_ptr = (void *)VA_RO_LC;

    /* Establish reservation at VA_RO, then change memory via VA_RW. */
    morello_store_ddc_addr(rw_ptr, 0x1111);
    {
        register void *x0 asm("x0") = ro_ptr;
        asm volatile("LDXR_CAP 2, 0" :: "r"(x0) : "x2", "memory");
    }
    morello_store_ddc_addr(rw_ptr, 0x2222);

    fault_count = 0;
    {
        register void *x0 asm("x0") = ro_ptr;
        register uintptr_t status asm("x4") = 0xdead;
        asm volatile(
            ".globl stxr_ro_insn\n\t"
            "stxr_ro_insn:\n\t"
            "STXR_CAP 4, 31, 0\n\t"
            : "+r"(status)
            : "r"(x0)
            : "memory");
    }
    check(fault_count == 1, 1);
    check(last_elr == (uintptr_t)stxr_ro_insn, 2);
    check(((last_esr >> 6) & 1) == 1 && (last_esr & 0x3f) == 0x0f, 2);

    /* Untagged SWP on RO + LC_TRAP page must fault as a store (WnR=1). */
    fault_count = 0;
    {
        register void *x6 asm("x6") = ro_lc_ptr;
        asm volatile(
            ".globl swp_ro_lc_insn\n\t"
            "swp_ro_lc_insn:\n\t"
            "SWP_CAP 31, 3, 6\n\t"
            :: "r"(x6)
            : "x3", "memory");
    }
    check(fault_count == 1, 3);
    check(last_elr == (uintptr_t)swp_ro_lc_insn, 4);
    check(((last_esr >> 6) & 1) == 1 && (last_esr & 0x3f) == 0x0f, 4);

    struct cap_info preserved = morello_load_cap(rw_ptr);
    check(preserved.tag && preserved.addr == 0x2222, 5);
    return 0;
}

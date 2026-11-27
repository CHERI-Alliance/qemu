/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Capability loads (LY) from MMIO and unmapped physical addresses must not
 * abort inside cheri_tag_get() when probe_read_fast() returns host == NULL.
 * - LY from CLINT mtimecmp (16-byte aligned MMIO at 0x02004000) must succeed
 *   with tag == 0.
 * - LY from an unmapped physical address (0x0) must raise a Load Access Fault
 *   (mcause == 5).
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 MMIO LY returned non-zero tag, 2 unmapped LY did not
 * trap, 3 unexpected trap cause.
 */
#include "rvy.h"

#define CLINT_MTIMECMP_ADDR 0x02004000UL
#define UNMAPPED_ADDR       0x00000000UL
#define CAUSE_LOAD_ACCESS   5UL

static volatile uintptr_t trap_count;
static volatile uintptr_t last_mcause;

__attribute__((aligned(16), naked)) static void trap_handler(void)
{
    asm volatile(
        "addi sp, sp, -24\n\t"
        "sd   t0, 0(sp)\n\t"
        "sd   t1, 8(sp)\n\t"
        "sd   t2, 16(sp)\n\t"
        "csrr t0, mcause\n\t"
        "la   t1, last_mcause\n\t"
        "sd   t0, 0(t1)\n\t"
        "la   t1, trap_count\n\t"
        "ld   t2, 0(t1)\n\t"
        "addi t2, t2, 1\n\t"
        "sd   t2, 0(t1)\n\t"
        "csrr t0, mepc\n\t"
        "addi t0, t0, 4\n\t"
        "csrw mepc, t0\n\t"
        "ld   t2, 16(sp)\n\t"
        "ld   t1, 8(sp)\n\t"
        "ld   t0, 0(sp)\n\t"
        "addi sp, sp, 24\n\t"
        "mret\n\t");
}

int test_main(void)
{
    uintptr_t handler_addr = (uintptr_t)trap_handler;
    asm volatile(
        "csrrs  t0, 0x03f, x0\n\t"
        "YADDRW t0, t0, %0\n\t"
        "csrw   mtvec, t0\n\t"
        :: "r"(handler_addr) : "t0", "memory");

    /* LY from MMIO must return tag == 0 without aborting or trapping. */
    trap_count = 0;
    uintptr_t val = 0;
    bool tag = rvy_load_cap_tag((const void *)CLINT_MTIMECMP_ADDR, &val);
    check(trap_count == 0, 3);
    check(!tag, 1);

    /* LY from unmapped physical memory must raise Load Access Fault. */
    trap_count = 0;
    last_mcause = 0;
    (void)rvy_load_cap_tag((const void *)UNMAPPED_ADDR, &val);
    check(trap_count == 1, 2);
    check(last_mcause == CAUSE_LOAD_ACCESS, 3);
    return 0;
}

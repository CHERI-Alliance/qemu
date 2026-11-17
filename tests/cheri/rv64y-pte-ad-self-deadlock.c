/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * When an unaligned scalar store straddles a 4 KiB page boundary [VA_CROSS,
 * VA_NEXT] where VA_CROSS maps the L0 page table itself (already dirty in the
 * TLB) and VA_NEXT's L0 PTE (residing in that same physical L0 page) still has
 * PTE.D == 0, probing VA_NEXT during tag invalidation triggers a hardware PTE
 * A/D update on l0_pt[1]. Tag invalidation and PTE A/D updates must not
 * re-lock a CheriTagBlock already held by the current hart.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 PTE.D not updated, 2 first page tag not cleared, 3
 * second page tag not cleared, 10 unexpected trap.
 */
#include "rvy.h"

#define VA_CROSS     0x40000000UL
#define VA_NEXT      0x40001000UL

#define SATP_SV39    (8ULL << 60)
#define MSTATUS_MPP  (3ULL << 11)
#define MSTATUS_MPP_S (1ULL << 11)
#define MSTATUS_MPRV (1ULL << 17)
#define MENVCFG_Y    (1ULL << 9)

#define PTE_V        (1ULL << 0)
#define PTE_R        (1ULL << 1)
#define PTE_W        (1ULL << 2)
#define PTE_A        (1ULL << 6)
#define PTE_D        (1ULL << 7)
#define PTE_Y        (1ULL << 58)

static uint64_t l2_pt[512] ALIGNED(PAGE_SIZE);
static uint64_t l1_pt[512] ALIGNED(PAGE_SIZE);
static uint64_t l0_pt[512] ALIGNED(PAGE_SIZE);
static uint64_t page_b[512] ALIGNED(PAGE_SIZE);

__attribute__((aligned(16), naked)) static void trap_handler(void)
{
    asm volatile(
        "li   t0, (1 << 17)\n\t"
        "csrc mstatus, t0\n\t"
        "li   a0, 10\n\t"
        "tail exit_test\n\t");
}

static uint64_t make_pte(const void *pa, uint64_t flags)
{
    return (((uintptr_t)pa >> 12) << 10) | flags;
}

int test_main(void)
{
    uintptr_t handler_addr = (uintptr_t)trap_handler;
    asm volatile(
        "csrrs t0, 0x03f, x0\n\t"
        "YADDRW t0, t0, %0\n\t"
        "csrw mtvec, t0\n\t"
        :: "r"(handler_addr) : "t0", "memory");

    l2_pt[1] = make_pte(l1_pt, PTE_V);
    l1_pt[0] = make_pte(l0_pt, PTE_V);
    l0_pt[0] = make_pte(l0_pt, PTE_Y | PTE_D | PTE_A | PTE_W | PTE_R | PTE_V);
    /* Leave PTE_D clear on l0_pt[1] so the cross-page store updates l0_pt. */
    l0_pt[1] = make_pte(page_b, PTE_Y | PTE_A | PTE_W | PTE_R | PTE_V);

    rvy_store_ddc_addr(&l0_pt[510], (uintptr_t)&l0_pt[510]);
    rvy_store_ddc_addr(&page_b[0], (uintptr_t)&page_b[0]);

    uint64_t satp = SATP_SV39 | ((uintptr_t)l2_pt >> 12);
    uint64_t menv = MENVCFG_Y;
    uint64_t pmpaddr = ~0ULL;
    uint64_t pmpcfg = 0x1fULL;
    asm volatile(
        "csrw pmpaddr0, %0\n\t"
        "csrw pmpcfg0, %1\n\t"
        "csrs 0x30a, %2\n\t"
        "csrw satp, %3\n\t"
        "sfence.vma\n\t"
        :: "r"(pmpaddr), "r"(pmpcfg), "r"(menv), "r"(satp) : "memory");

    uint64_t mprv_s = MSTATUS_MPRV | MSTATUS_MPP_S;
    uint64_t mpp_clear = MSTATUS_MPP | MSTATUS_MPRV;
    uint64_t val = 0x1122334455667788ULL;
    asm volatile(
        "csrc mstatus, %0\n\t"
        "csrs mstatus, %1\n\t"
        /* Populate VA_CROSS in the S-mode TLB without touching VA_NEXT. */
        "sd   zero, -32(%2)\n\t"
        /* Unaligned store across [VA_CROSS + 0xffc .. VA_NEXT + 0x003]. */
        "sd   %3, -4(%2)\n\t"
        "csrc mstatus, %0\n\t"
        :: "r"(mpp_clear), "r"(mprv_s), "r"(VA_NEXT), "r"(val)
        : "memory");

    check((l0_pt[1] & PTE_D) != 0, 1);
    check(!rvy_load_cap_tag(&l0_pt[510], NULL), 2);
    check(!rvy_load_cap_tag(&page_b[0], NULL), 3);
    return 0;
}

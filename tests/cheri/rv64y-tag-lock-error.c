/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * When a scalar store's non-faulting pre-store tag-lock probe sees
 * TLB_INVALID_MASK (e.g., because another hart is concurrently toggling PTE.W)
 * and records TAG_LOCK_ERROR, if the subsequent scalar store succeeds without
 * faulting, invalidate_from_locktag() must still clear the capability tag.
 *
 * Hart 0 loops storing tagged capabilities to data_page, flushing its TLB,
 * and issuing aligned and unaligned scalar stores via Sv39 (MPRV=1, MPP=S),
 * while Harts 1..3 concurrently toggle PTE.W on the leaf PTE. Whenever a
 * scalar store overwrites a capability's bytes, its tag must be 0.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 aligned store left tag set, 2 unaligned store left
 * tag set, 10 unexpected trap.
 */
#include "rvy.h"

#define VA_DATA       0x40000000UL
#define SATP_SV39     (8ULL << 60)
#define MSTATUS_MPP   (3ULL << 11)
#define MSTATUS_MPP_S (1ULL << 11)
#define MSTATUS_MPRV  (1ULL << 17)
#define MENVCFG_Y     (1ULL << 9)

#define PTE_V         (1ULL << 0)
#define PTE_R         (1ULL << 1)
#define PTE_W         (1ULL << 2)
#define PTE_A         (1ULL << 6)
#define PTE_D         (1ULL << 7)
#define PTE_Y         (1ULL << 58)

#define SENTINEL      0xdeadbeefcafebabeULL
#define ITERS         4000

static uint64_t l2_pt[512] ALIGNED(PAGE_SIZE);
static uint64_t l1_pt[512] ALIGNED(PAGE_SIZE);
static uint64_t l0_pt[512] ALIGNED(PAGE_SIZE);
static uint8_t data_page[PAGE_SIZE] ALIGNED(PAGE_SIZE);

static uint64_t pte_rx;
static uint64_t pte_rwx;
static uint32_t start_bar;
static volatile uint32_t stop_flag;

__attribute__((aligned(16), naked)) static void trap_handler(void)
{
    asm volatile(
        "csrw mscratch, t0\n\t"
        "csrr t0, mcause\n\t"
        "addi t0, t0, -15\n\t"
        "bnez t0, 1f\n\t"
        "csrr t0, mepc\n\t"
        "addi t0, t0, 4\n\t"
        "csrw mepc, t0\n\t"
        "csrr t0, mscratch\n\t"
        "mret\n\t"
        "1:\n\t"
        "li   t0, (1 << 17)\n\t"
        "csrc mstatus, t0\n\t"
        "li   a0, 10\n\t"
        "tail exit_test\n\t");
}

static uint64_t make_pte(const void *pa, uint64_t flags)
{
    return (((uintptr_t)pa >> 12) << 10) | flags;
}

void secondary_main(unsigned long cpu_id)
{
    (void)cpu_id;
    barrier_wait(&start_bar, 4);
    while (!atomic_load_acquire(&stop_flag)) {
        atomic_store_release(&l0_pt[0], pte_rx);
        atomic_store_release(&l0_pt[0], pte_rwx);
    }
    while (1) {
        asm volatile("wfi");
    }
}

int test_main(void)
{
    uintptr_t handler_addr = (uintptr_t)trap_handler;
    asm volatile(
        "csrrs  t0, 0x03f, x0\n\t"
        "YADDRW t0, t0, %0\n\t"
        "csrw   mtvec, t0\n\t"
        :: "r"(handler_addr) : "t0", "memory");

    pte_rx = make_pte(data_page, PTE_Y | PTE_D | PTE_A | PTE_R | PTE_V);
    pte_rwx = make_pte(data_page,
                       PTE_Y | PTE_D | PTE_A | PTE_W | PTE_R | PTE_V);
    l2_pt[1] = make_pte(l1_pt, PTE_V);
    l1_pt[0] = make_pte(l0_pt, PTE_V);
    l0_pt[0] = pte_rwx;

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

    barrier_wait(&start_bar, 4);

    uint64_t mprv_s = MSTATUS_MPRV | MSTATUS_MPP_S;
    uint64_t mpp_clear = MSTATUS_MPP | MSTATUS_MPRV;
    uint64_t sentinel = SENTINEL;

    for (int i = 0; i < ITERS; i++) {
        rvy_store_ddc_cap(&data_page[0], (uintptr_t)data_page, PAGE_SIZE,
                          (uintptr_t)data_page);
        rvy_store_ddc_cap(&data_page[16], (uintptr_t)data_page, PAGE_SIZE,
                          (uintptr_t)data_page + 16);

        asm volatile(
            "csrc mstatus, %0\n\t"
            "csrs mstatus, %1\n\t"
            "sfence.vma\n\t"
            "sd   %3, 0(%2)\n\t"
            "sfence.vma\n\t"
            "sd   %3, 12(%2)\n\t"
            "csrc mstatus, %0\n\t"
            :: "r"(mpp_clear), "r"(mprv_s), "r"(VA_DATA), "r"(sentinel)
            : "memory");

        uint64_t w0 = *(volatile uint64_t *)&data_page[0];
        if (w0 == SENTINEL) {
            check(!rvy_load_cap_tag(&data_page[0], NULL), 1);
        }

        uint64_t w1 = *(volatile uint64_t *)&data_page[16];
        if ((uint32_t)w1 == (uint32_t)(SENTINEL >> 32)) {
            check(!rvy_load_cap_tag(&data_page[16], NULL), 2);
        }
    }

    atomic_store_release(&stop_flag, 1);
    return 0;
}

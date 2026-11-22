/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * RISC-V hardware page-table walk A/D bit updates (target/riscv/cpu_helper.c)
 * must not clear a capability tag on the PTE granule if qatomic_cmpxchg fails
 * because another hart concurrently replaced the PTE with a tagged capability.
 *
 * Each round:
 * - Hart 0 installs a valid leaf PTE with PTE.A == 0 at l0_pt[0], releases
 *   Hart 1, runs sfence.vma, and loads VA_DATA under MPRV=1 (triggering a PTW
 *   A-bit update on &l0_pt[0]).
 * - Hart 1 concurrently stores a tagged capability (with bit 0 == 0 so PTE.V=0)
 *   once to &l0_pt[0] via SY, then waits for the next round.
 * - After both finish the round, if l0_pt[0] still holds Hart 1's capability
 *   address (meaning Hart 0's cmpxchg did not overwrite it), its tag must still
 *   be 1.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 failed PTW cmpxchg stripped capability tag, 10
 * unexpected trap.
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
#define PTE_Y         (1ULL << 58)

/* Bit 0 is 0 so if Hart 0 reads this as a PTE it takes a Load Page Fault. */
#define CAP_MARKER    0xdeadbeef00000000ULL
#define ROUNDS        80000

static uint64_t l2_pt[512] ALIGNED(PAGE_SIZE);
static uint64_t l1_pt[512] ALIGNED(PAGE_SIZE);
static uint64_t l0_pt[512] ALIGNED(PAGE_SIZE);
static uint64_t data_page[512] ALIGNED(PAGE_SIZE);

static struct cap_slot marker_cap;
static uint32_t start_bar;
static volatile uint32_t round_go;
static volatile uint32_t round_done;
static volatile uint32_t stop_flag;

__attribute__((aligned(16), naked)) static void trap_handler(void)
{
    asm volatile(
        "csrw mscratch, t0\n\t"
        "csrr t0, mcause\n\t"
        "addi t0, t0, -13\n\t"
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
    barrier_wait(&start_bar, 2);
    uint32_t seen = 0;
    while (!atomic_load_acquire(&stop_flag)) {
        uint32_t cur = atomic_load_acquire(&round_go);
        if (cur == seen) {
            continue;
        }
        seen = cur;
        /* Small varying spin to sweep across Hart 0's PTW cmpxchg window. */
        uint32_t delay = cur & 31;
        for (volatile uint32_t d = 0; d < delay; d++) {
        }
        asm volatile(
            "LY t0, 0(%0)\n\t"
            "SY t0, 0(%1)\n\t"
            :: "r"(&marker_cap), "r"(&l0_pt[0]) : "t0", "memory");
        atomic_store_release(&round_done, seen);
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

    rvy_store_ddc_addr(&marker_cap, CAP_MARKER);
    /* Pre-allocate l0_pt's CheriTagBlock before the race loop starts. */
    rvy_store_ddc_addr(&l0_pt[0], 0);

    uint64_t pte_no_a = make_pte(data_page, PTE_Y | PTE_W | PTE_R | PTE_V);
    l2_pt[1] = make_pte(l1_pt, PTE_V);
    l1_pt[0] = make_pte(l0_pt, PTE_V);
    l0_pt[0] = pte_no_a;

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

    barrier_wait(&start_bar, 2);

    uint64_t mprv_s = MSTATUS_MPRV | MSTATUS_MPP_S;
    uint64_t mpp_clear = MSTATUS_MPP | MSTATUS_MPRV;

    for (uint32_t r = 1; r <= ROUNDS; r++) {
        l0_pt[0] = pte_no_a;
        asm volatile("sfence.vma" ::: "memory");
        atomic_store_release(&round_go, r);

        uint64_t tmp;
        asm volatile(
            "csrc mstatus, %1\n\t"
            "csrs mstatus, %2\n\t"
            "ld   %0, 0(%3)\n\t"
            "csrc mstatus, %1\n\t"
            : "=&r"(tmp)
            : "r"(mpp_clear), "r"(mprv_s), "r"(VA_DATA)
            : "memory");

        while (atomic_load_acquire(&round_done) != r) {
        }

        uintptr_t addr = 0;
        bool tag = rvy_load_cap_tag(&l0_pt[0], &addr);
        if (addr == CAP_MARKER) {
            check(tag, 1);
        }
    }

    atomic_store_release(&stop_flag, 1);
    return 0;
}

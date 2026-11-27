/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Morello 32-byte capability-pair LDXP must return a consistent pair of
 * capabilities rather than tearing across a concurrent atomic pair update.
 *
 * CPU 1 loops alternating pair_slot[0..1] between (CAP_A, CAP_A) and
 * (CAP_B, CAP_B) via LDXP_CAP + STXP_CAP (which holds a single tag lock across
 * both 16-byte stores). CPU 0 loops LDXP_CAP on pair_slot and checks that both
 * loaded capabilities are tagged and carry the same address.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 secondary CPU failed to boot, 2 LDXP observed
 * untagged capability, 3 LDXP observed torn capability pair.
 */
#include "morello.h"

#define CAP_A_ADDR 0x0000aaaa0000ULL
#define CAP_B_ADDR 0x0000bbbb0000ULL
#define READ_ITERS 200000

static struct cap_slot pair_slot[2] ALIGNED(32);
static struct cap_slot cap_a_slot;
static struct cap_slot cap_b_slot;
static uint32_t start_bar;
static volatile uint32_t stop_flag;

static inline void morello_ldxp_pair_check(const void *slot,
                                           uintptr_t *a0_out, bool *t0_out,
                                           uintptr_t *a1_out, bool *t1_out)
{
    register const void *x0 asm("x0") = slot;
    register uintptr_t a0 asm("x1");
    register uintptr_t t0 asm("x2");
    register uintptr_t a1 asm("x3");
    register uintptr_t t1 asm("x4");
    asm volatile(
        "LDXP_CAP 5, 6, 0\n\t"
        "GCTAG    2, 5\n\t"
        "GCTAG    4, 6\n\t"
        "mov      x1, x5\n\t"
        "mov      x3, x6\n\t"
        : "=r"(a0), "=r"(t0), "=r"(a1), "=r"(t1)
        : "r"(x0)
        : "x5", "x6", "memory");
    *a0_out = a0;
    *t0_out = (t0 != 0);
    *a1_out = a1;
    *t1_out = (t1 != 0);
}

static inline void morello_stxp_pair_set(void *slot, const void *cap_slot_src)
{
    register void *x0 asm("x0") = slot;
    register const void *x1 asm("x1") = cap_slot_src;
    asm volatile(
        "LDR_CAP  2, 1, 0\n\t"
        "1:\n\t"
        "LDXP_CAP 3, 4, 0\n\t"
        "STXP_CAP 5, 2, 2, 0\n\t"
        "cbnz     w5, 1b\n\t"
        :: "r"(x0), "r"(x1)
        : "x2", "x3", "x4", "x5", "memory");
}

void secondary_main(unsigned long cpu_id)
{
    (void)cpu_id;
    barrier_wait(&start_bar, 2);
    while (!atomic_load_acquire(&stop_flag)) {
        morello_stxp_pair_set(pair_slot, &cap_b_slot);
        morello_stxp_pair_set(pair_slot, &cap_a_slot);
    }
    while (1) {
        asm volatile("wfe");
    }
}

int test_main(void)
{
    morello_store_ddc_addr(&cap_a_slot, CAP_A_ADDR);
    morello_store_ddc_addr(&cap_b_slot, CAP_B_ADDR);
    morello_store_ddc_addr(&pair_slot[0], CAP_A_ADDR);
    morello_store_ddc_addr(&pair_slot[1], CAP_A_ADDR);

    check(morello_cpu_on(1) == 0, 1);
    barrier_wait(&start_bar, 2);

    for (int i = 0; i < READ_ITERS; i++) {
        uintptr_t a0, a1;
        bool t0, t1;
        morello_ldxp_pair_check(pair_slot, &a0, &t0, &a1, &t1);
        check(t0 && t1, 2);
        check(a0 == a1 && (a0 == CAP_A_ADDR || a0 == CAP_B_ADDR), 3);
    }

    atomic_store_release(&stop_flag, 1);
    return 0;
}

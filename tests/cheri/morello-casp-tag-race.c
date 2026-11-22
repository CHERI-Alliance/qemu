/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * AArch64 128-bit CASP (gen_compare_and_swap_pair) must hold the capability
 * tag lock across the full load-compare-store sequence and must not write back
 * stale data over a concurrently stored tagged capability when the comparison
 * fails.
 *
 * Phase 1: CPU 1 alternates storing a tagged capability (VAL_CAP) and an
 * untagged scalar pair (SCALAR_LO, SCALAR_HI) to target while CPU 0 hammers
 * target with a non-matching CASPAL. If CASP loads the scalar before locking
 * and writes it back on failure without clearing the tag, a subsequent load
 * observes tag == 1 with the untagged scalar payload.
 *
 * Phase 2: Both CPUs increment a 128-bit counter via CASPAL; final count must
 * equal 2 * COUNTER_ITERS without lost updates.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 secondary CPU failed to boot, 2 failing CASP
 * corrupted a tagged capability (tag=1 with non-capability payload), 3 CASP
 * 128-bit counter lost updates.
 */
#include "morello.h"

#define CAP_ADDR       0x00001000ULL
#define SCALAR_LO      0xdeadbeefcafebabeULL
#define SCALAR_HI      0x1122334455667788ULL
#define NEVER_MATCH_LO 0x0101010101010101ULL
#define NEVER_MATCH_HI 0x0202020202020202ULL

#define RACE_ITERS     40000
#define COUNTER_ITERS  20000

static struct cap_slot target;
static struct cap_slot counter;
static uint32_t bar1;
static uint32_t bar2;
static uint32_t bar3;
static volatile uint32_t stop_phase1;

static inline bool caspal_128(void *ptr, uint64_t *exp_lo, uint64_t *exp_hi,
                              uint64_t new_lo, uint64_t new_hi)
{
    register void *x0 asm("x0") = ptr;
    register uint64_t x2 asm("x2") = *exp_lo;
    register uint64_t x3 asm("x3") = *exp_hi;
    register uint64_t x4 asm("x4") = new_lo;
    register uint64_t x5 asm("x5") = new_hi;
    uint64_t orig_lo = *exp_lo;
    uint64_t orig_hi = *exp_hi;

    asm volatile(
        "CASPAL_X 2, 4, 0\n\t"
        : "+r"(x2), "+r"(x3)
        : "r"(x0), "r"(x4), "r"(x5)
        : "memory");

    *exp_lo = x2;
    *exp_hi = x3;
    return x2 == orig_lo && x3 == orig_hi;
}

static void run_counter_inc(void)
{
    for (int i = 0; i < COUNTER_ITERS; i++) {
        uint64_t lo = atomic_load_acquire(&counter.words[0]);
        uint64_t hi = atomic_load_acquire(&counter.words[1]);
        while (!caspal_128(&counter, &lo, &hi, lo + 1, hi + 1)) {
        }
    }
}

void secondary_main(unsigned long cpu_id)
{
    (void)cpu_id;

    /* Phase 1: alternate tagged capability and untagged scalar pair. */
    barrier_wait(&bar1, 2);
    while (!atomic_load_acquire(&stop_phase1)) {
        morello_store_ddc_addr(&target, CAP_ADDR);
        struct cap_info info = morello_load_cap(&target);
        if (info.tag) {
            check(info.addr == CAP_ADDR, 2);
        }
        morello_store_pair_u64(&target, SCALAR_LO, SCALAR_HI);
    }

    /* Phase 2: concurrent 128-bit CASP counter increments. */
    barrier_wait(&bar2, 2);
    run_counter_inc();
    barrier_wait(&bar3, 2);

    while (1) {
        asm volatile("wfe");
    }
}

int test_main(void)
{
    morello_store_ddc_addr(&target, CAP_ADDR);
    check(morello_cpu_on(1) == 0, 1);

    /* Phase 1: hammer target with non-matching CASPAL and check invariant. */
    barrier_wait(&bar1, 2);
    for (int i = 0; i < RACE_ITERS; i++) {
        uint64_t lo = NEVER_MATCH_LO;
        uint64_t hi = NEVER_MATCH_HI;
        (void)caspal_128(&target, &lo, &hi, 0x3333, 0x4444);
        struct cap_info info = morello_load_cap(&target);
        if (info.tag) {
            check(info.addr == CAP_ADDR, 2);
        }
    }
    atomic_store_release(&stop_phase1, 1);

    /* Phase 2: concurrent 128-bit CASP counter increments. */
    counter.words[0] = 0;
    counter.words[1] = 0;
    barrier_wait(&bar2, 2);
    run_counter_inc();
    barrier_wait(&bar3, 2);

    check(counter.words[0] == 2 * COUNTER_ITERS &&
          counter.words[1] == 2 * COUNTER_ITERS, 3);
    return 0;
}

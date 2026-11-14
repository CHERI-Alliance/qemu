/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Exercises concurrent capability loads against capability stores, partial
 * scalar stores, and scalar AMOs on the same granule, plus concurrent tag
 * updates on four adjacent granules sharing a single 64-byte cacheline and
 * tag-bitmap word.
 *
 * A broken seqlock reader (missing odd-sequence retry or acquire barrier) or
 * a scalar store/AMO path that skips the lock when a tag bit appears clear
 * exposes torn or corrupted capabilities with tag == 1, or loses adjacent
 * tag-bit updates in a shared bitmap word.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 torn or forged capability observed with tag == 1,
 * 2 adjacent-granule capability lost its tag or cursor, 3 adjacent-granule
 * scalar/AMO store failed to clear its tag.
 */
#include "rvy.h"

#define NHARTS     4
#define READ_ITERS 60000
#define ADJ_ITERS  30000

#define CAP_A_LEN  0x100UL
#define CAP_A_OFF  0x20UL
#define CAP_B_BASE 0x200UL
#define CAP_B_LEN  0x400UL
#define CAP_B_OFF  0x280UL

static struct {
    struct cap_slot shared_slot;
    struct cap_slot pad[3];
    struct cap_slot adj_slots[NHARTS];
} test_granules ALIGNED(PAGE_SIZE);

static uint8_t arena[PAGE_SIZE] ALIGNED(PAGE_SIZE);

static struct {
    struct cap_slot cap_a;
    struct cap_slot cap_b;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t readers_done;
    volatile uint32_t stop_a;
} sync_state ALIGNED(PAGE_SIZE);

static void run_scalar_writer(void)
{
    volatile uint64_t *words =
        (volatile uint64_t *)&test_granules.shared_slot;
    volatile uint8_t *bytes =
        (volatile uint8_t *)&test_granules.shared_slot;
    uint32_t *hi_word32 = (uint32_t *)&words[1];

    while (!sync_state.stop_a) {
        words[0] = 0xdeadbeefcafebabeULL;
        words[1] = 0xfeedface12345678ULL;
        bytes[3] = 0x5a;
        atomic_fetch_add_relaxed(&words[0], 0x1000ULL);
        atomic_fetch_xor_seq_cst(hi_word32, 0x5aU);
        rvy_cbo_zero((void *)&test_granules.shared_slot);
    }
}

static void run_cap_reader(uintptr_t base_a, uintptr_t cur_a,
                           uintptr_t base_b, uintptr_t cur_b)
{
    for (int i = 0; i < READ_ITERS; i++) {
        struct cap_info c = rvy_load_cap(&test_granules.shared_slot);
        if (!c.tag) {
            continue;
        }
        bool match_a = (c.addr == cur_a && c.base == base_a &&
                        c.len == CAP_A_LEN);
        bool match_b = (c.addr == cur_b && c.base == base_b &&
                        c.len == CAP_B_LEN);
        check(match_a || match_b, 1);
    }
    if (atomic_fetch_add_seq_cst(&sync_state.readers_done, 1) + 1 == 2) {
        atomic_store_release(&sync_state.stop_a, 1);
    }
}

static void run_adjacent_granules_phase(unsigned long hartid)
{
    struct cap_slot *my_slot = &test_granules.adj_slots[hartid];
    uintptr_t slot_addr = (uintptr_t)my_slot;
    volatile uint8_t *byte_ptr = (volatile uint8_t *)my_slot + 5;
    uint32_t *word_ptr = (uint32_t *)my_slot;

    for (int i = 0; i < ADJ_ITERS; i++) {
        rvy_store_ddc_addr(my_slot, slot_addr);

        uintptr_t loaded_addr = 0;
        bool tagged = rvy_load_cap_tag(my_slot, &loaded_addr);
        check(tagged && loaded_addr == slot_addr, 2);

        if ((i & 1) == 0) {
            *byte_ptr = 0;
        } else {
            atomic_fetch_add_seq_cst(word_ptr, 1U);
        }

        check(!rvy_load_cap_tag(my_slot, NULL), 3);
    }
}

static void hart_main(unsigned long hartid)
{
    if (hartid >= NHARTS) {
        return;
    }

    uintptr_t base_a = (uintptr_t)arena;
    uintptr_t cur_a = base_a + CAP_A_OFF;
    uintptr_t base_b = base_a + CAP_B_BASE;
    uintptr_t cur_b = base_a + CAP_B_OFF;

    if (hartid == 0) {
        rvy_store_ddc_cap(&sync_state.cap_a, base_a, CAP_A_LEN, cur_a);
        rvy_store_ddc_cap(&sync_state.cap_b, base_b, CAP_B_LEN, cur_b);
    }
    barrier_wait(&sync_state.bar1, NHARTS);

    if (hartid == 1) {
        rvy_alternate_sy_until_stop(&test_granules.shared_slot,
                                    &sync_state.cap_a, &sync_state.cap_b,
                                    &sync_state.stop_a);
    } else if (hartid == 2) {
        run_scalar_writer();
    } else {
        run_cap_reader(base_a, cur_a, base_b, cur_b);
    }

    barrier_wait(&sync_state.bar2, NHARTS);
    run_adjacent_granules_phase(hartid);
    barrier_wait(&sync_state.bar3, NHARTS);
}

void secondary_main(unsigned long hartid)
{
    hart_main(hartid);
}

int test_main(void)
{
    hart_main(0);
    return 0;
}

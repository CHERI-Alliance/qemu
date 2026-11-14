/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A paired store whose first half writes the last capability granule of RAM
 * and whose second half takes an external abort must still clear the tag of
 * that granule. Checks STP of two X registers and a 128-bit STR of a Q
 * register (both split into two 64-bit stores by QEMU).
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Expects -m 128M (RAM ends at 0x48000000).
 * Exit status: 0 pass, 1 STP left the tag set, 2 STR Q left the tag set,
 * 3 capability store did not set the tag, 4 no abort taken, 5 first half
 * not written, 10 unexpected exception.
 */
#include "morello.h"

#define RAM_END 0x48000000UL

static void store_tagged_cap(void *granule)
{
    morello_store_ddc_addr(granule, (uintptr_t)granule);
    check(morello_load_cap(granule).tag, 3);
    morello_fault_count = 0;
}

static void check_aborted_and_tag_cleared(void *granule,
                                          const volatile uint64_t *first_half,
                                          uint64_t expected, int fail_code)
{
    check(morello_fault_count != 0, 4);
    check(*first_half == expected, 5);
    check(!morello_load_cap(granule).tag, fail_code);
}

int test_main(void)
{
    void *last_granule = (void *)(RAM_END - 16);
    uint64_t *straddle_ptr = (uint64_t *)(RAM_END - 8);
    uint64_t v1 = 0x1111;
    uint64_t v2 = 0x2222;

    store_tagged_cap(last_granule);
    morello_store_pair_u64(straddle_ptr, v1, v2);
    check_aborted_and_tag_cleared(last_granule, straddle_ptr, v1, 1);

    store_tagged_cap(last_granule);
    morello_store_q128(straddle_ptr, v1, v2);
    check_aborted_and_tag_cleared(last_granule, straddle_ptr, v1, 2);
    return 0;
}

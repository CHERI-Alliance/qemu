/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A load or store through the full-address-space reset DDC whose range wraps
 * past 2^64 must take a capability bounds fault (DFSC 0x2a).
 * CapIsRangeInBounds uses 65-bit arithmetic, so 8 bytes at 2^64 - 4 end above
 * the DDC top, and the bounds check takes priority over translation faults.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 load did not fault, 2 store did not fault,
 * 3 not a data abort, 0x80 + n data abort with DFSC n.
 */
#include "morello.h"

static volatile uint64_t bounds_faults;

bool on_sync_exception(uint64_t esr, uint64_t elr)
{
    (void)elr;
    uint64_t ec = (esr >> 26) & 0x3f;
    uint64_t dfsc = esr & 0x3f;

    check(ec == 0x25, 3);
    check(dfsc == 0x2a, (int)(0x80 | dfsc));
    bounds_faults++;
    return true;
}

int test_main(void)
{
    void *wrap_addr = (void *)(uintptr_t)-4ULL;

    (void)morello_load_u64(wrap_addr);
    check(bounds_faults == 1, 1);

    morello_store_u64(wrap_addr, 0);
    check(bounds_faults == 2, 2);
    return 0;
}

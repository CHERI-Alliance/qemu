/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * On RV32Y (8-byte capabilities, 512 tags per 4 KiB page), CheriTagBlock
 * bitmaps are 64 bytes each rather than 32 bytes, so adding extra fields to
 * CheriTagBlock makes sizeof(CheriTagBlock) a non-power-of-two multiple of
 * 64 (such as 192 bytes). Verify that allocating CheriTagBlock structures on
 * 32-bit CHERI does not abort or corrupt adjacent tag blocks.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 tag not set by first SY, 2 tag not set by second SY.
 */
#include "rvy.h"

static uint8_t buf[PAGE_SIZE] ALIGNED(PAGE_SIZE);

int test_main(void)
{
    rvy_store_ddc_addr(&buf[0], (uintptr_t)buf);
    rvy_store_ddc_addr(&buf[8], (uintptr_t)buf);

    check(rvy_load_cap_tag(&buf[0], NULL), 1);
    check(rvy_load_cap_tag(&buf[8], NULL), 2);
    return 0;
}

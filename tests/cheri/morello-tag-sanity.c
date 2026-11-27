/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Sanity check for Morello tagged memory: a capability derived from DDC and
 * stored with STR loads back tagged, and a one-byte scalar store into the same
 * granule clears the tag.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 tag not set after STR, 2 tag not cleared by STRB.
 */
#include "morello.h"

static volatile uint8_t buf[16] ALIGNED(16);

int test_main(void)
{
    morello_store_ddc_addr((void *)buf, (uintptr_t)buf);
    check(morello_load_cap((const void *)buf).tag, 1);

    buf[3] = 0;
    check(!morello_load_cap((const void *)buf).tag, 2);
    return 0;
}

/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Sanity check for RV64Y tagged memory: a capability derived from DDC and
 * stored with SY loads back tagged, and a one-byte scalar store into the same
 * granule clears the tag.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 tag not set after SY, 2 tag not cleared by SB.
 */
#include "rvy.h"

static uint8_t buf[16] ALIGNED(16);

int test_main(void)
{
    rvy_store_ddc_addr(buf, (uintptr_t)buf);
    check(rvy_load_cap_tag(buf, NULL), 1);

    buf[3] = 0;
    check(!rvy_load_cap_tag(buf, NULL), 2);
    return 0;
}

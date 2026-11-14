/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * The first tagged store to memory without a tag block allocates one, and
 * with MTTCG and more than one hart QEMU later marks it synced. The whole
 * CheriTagBlock, including the synced flag, must be inside that allocation.
 * Any out-of-bounds access is reported by ASan on the host.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 tag not set by the first SY, 2 tag not set by the
 * second SY to the same tag block.
 */
#include "rvy.h"

static uint8_t buf[PAGE_SIZE] ALIGNED(PAGE_SIZE);

int test_main(void)
{
    rvy_store_ddc_addr(&buf[0], (uintptr_t)buf);
    rvy_store_ddc_addr(&buf[16], (uintptr_t)buf);

    check(rvy_load_cap_tag(&buf[0], NULL), 1);
    check(rvy_load_cap_tag(&buf[16], NULL), 2);
    return 0;
}

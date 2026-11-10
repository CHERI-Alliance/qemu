/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Every capability load logs "Cap Tag Read" under -d instr, including loads
 * from memory whose tag block was never allocated (ALL_ZERO_TAGBLK).
 * meson.build checks the log for the load from FRESH_ADDR, which lies in a
 * 64 KiB tag block that never held a tag.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 loaded capability is tagged.
 */
#include "rvy.h"

#define FRESH_ADDR 0x80400000UL

int test_main(void)
{
    check(!rvy_load_cap_tag((const void *)FRESH_ADDR, NULL), 1);
    return 0;
}

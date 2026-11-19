/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Guest stub for rv64y-dummy-lock-collision.gdb, which breaks on main() and
 * calls get_dummy_locks() directly before letting the guest exit cleanly.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 */
#include "rvy.h"

int test_main(void)
{
    return 0;
}

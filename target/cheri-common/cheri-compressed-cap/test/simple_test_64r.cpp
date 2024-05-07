
#define TEST_CC_FORMAT_LOWER 64r
#define TEST_CC_FORMAT_UPPER 64R

#include "test_common.cpp"
#include "cap_ap.h"

TEST_CASE("update sealed", "[sealed]") {
    _cc_cap_t cap;

    _cc_N(update_sealed)(&cap, 1);
    CHECK_FIELD(cap, is_sealed, 1);
}

/*
 * AP compression
 */

TEST_CASE_AP_COMP(CAP_AP_R, (CAP_AP_Q0 | 1))
TEST_CASE_AP_COMP(CAP_AP_W, (CAP_AP_Q0 | 4))
TEST_CASE_AP_COMP(CAP_AP_R | CAP_AP_W, (CAP_AP_Q0 | 5))

TEST_CASE_AP_COMP(
        (CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR),
        (CAP_AP_Q1 | 0))
TEST_CASE_AP_COMP(
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR),
        (CAP_AP_Q1 | 1))
TEST_CASE_AP_COMP(
        (CAP_AP_X | CAP_AP_R | CAP_AP_C),
        (CAP_AP_Q1 | 2))
TEST_CASE_AP_COMP(
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_C),
        (CAP_AP_Q1 | 3))
TEST_CASE_AP_COMP(
        (CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C),
        (CAP_AP_Q1 | 4))
TEST_CASE_AP_COMP(
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C),
        (CAP_AP_Q1 | 5))
TEST_CASE_AP_COMP(
        (CAP_AP_X | CAP_AP_R | CAP_AP_W),
        (CAP_AP_Q1 | 6))
TEST_CASE_AP_COMP(
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W),
        (CAP_AP_Q1 | 7))

TEST_CASE_AP_COMP(CAP_AP_C | CAP_AP_R, (CAP_AP_Q3 | 3))
TEST_CASE_AP_COMP(CAP_AP_C | CAP_AP_R | CAP_AP_W, (CAP_AP_Q3 | 7))

/* Invalid permissions must result in "no permissions". */
TEST_CASE_AP_COMP(CAP_AP_C, 0);

/*
 * AP decompression
 */

TEST_CASE_AP_DECOMP(CAP_AP_Q0 | 1, CAP_AP_R)
TEST_CASE_AP_DECOMP(CAP_AP_Q0 | 4, CAP_AP_W)
TEST_CASE_AP_DECOMP(CAP_AP_Q0 | 5, (CAP_AP_R | CAP_AP_W))
/* invalid in Q0 -> no permissions */
TEST_CASE_AP_DECOMP(CAP_AP_Q0 | 6, 0)

TEST_CASE_AP_DECOMP(
        (CAP_AP_Q1 | 0),
        (CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR))
TEST_CASE_AP_DECOMP(
        (CAP_AP_Q1 | 1),
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR))
TEST_CASE_AP_DECOMP(
        (CAP_AP_Q1 | 2),
        (CAP_AP_X | CAP_AP_R | CAP_AP_C))
TEST_CASE_AP_DECOMP(
        (CAP_AP_Q1 | 3),
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_C))
TEST_CASE_AP_DECOMP(
        (CAP_AP_Q1 | 4),
        (CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C))
TEST_CASE_AP_DECOMP(
        (CAP_AP_Q1 | 5),
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C))
TEST_CASE_AP_DECOMP(
        (CAP_AP_Q1 | 6),
        (CAP_AP_X | CAP_AP_R | CAP_AP_W))
TEST_CASE_AP_DECOMP(
        (CAP_AP_Q1 | 7),
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W))

/*
 * invalid in Q2 -> all permissions (infinite capability does not have the
 * M-bit set)
 */
TEST_CASE_AP_DECOMP((CAP_AP_Q2 | 2),
        (CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR))

TEST_CASE_AP_DECOMP((CAP_AP_Q3 | 3), CAP_AP_C | CAP_AP_R)
TEST_CASE_AP_DECOMP((CAP_AP_Q3 | 7), CAP_AP_C | CAP_AP_R | CAP_AP_W)
/* invalid in Q3 -> all permissions (no M-bit, see above) */
TEST_CASE_AP_DECOMP((CAP_AP_Q3 | 1),
        (CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR))

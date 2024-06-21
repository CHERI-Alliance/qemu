
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


TEST_CASE("bounds encoding, internal exponent, L8 = 1", "[bounds]") {
    /* params are base, cursor, top */
    _cc_cap_t cap = CompressedCap64r::make_max_perms_cap(0x0, 0x1000, 0x8000);

    /*
     * 00 SDP
     * 01000 AP quadrant 1, execute + ASR
     * 0000 reserved
     * 0 S
     * 0 EF
     * 1 L8
     * 000000 T[7:2]
     * 00 TE
     * 00000000 B[9:2]
     * 01 BE
     *
     * internal exponent, E = 24 - 0b10001 = 7
     * LCout = 0, LMSB = 1
     * c_t = 0, c_b = 0
     */
    CHECK(cap.cr_pesbt == 0x10040001);
}

TEST_CASE("bounds encoding, exponent > 0, T8==0", "[bounds]") {
    _cc_cap_t cap = CompressedCap64r::make_max_perms_cap(0x4C000000,
            0x90000000, 0xAC000000);

    /*
     * 00 SDP
     * 01000 AP quadrant 1, execute + ASR
     * 0000 reserved
     * 0 S
     * 0 EF
     * 0 L8
     * 101100 T[7:2]
     * 00 TE
     * 01001100 B[9:2]
     * 10 BE
     *
     * internal exponent, E = 24 - 0b00010 = 22
     * LCout = 0, LMSB = 1
     * c_t = 0, c_b = 0
     */
    CHECK(cap.cr_pesbt == 0x1002c132);
}

TEST_CASE("bounds encoding, exponent > 0, T8==0, c_b==-1", "[bounds]") {
    _cc_cap_t cap = CompressedCap64r::make_max_perms_cap(0x9F000000,
            0xA0000000, 0xAC000000);

    /*
     * 00 SDP
     * 01000 AP quadrant 1, execute + ASR
     * 0000 reserved
     * 0 S
     * 0 EF
     * 0 L8
     * 100000 T[7:2]
     * 01 TE
     * 11111000 B[9:2]
     * 01 BE
     *
     * internal exponent, E = 24 - 0b00101 = 19
     * LCout = 1, LMSB = 1
     * A < R is true, B < R is false, T < R is true
     * c_t = 0, c_b = -1
     */
     CHECK(cap.cr_pesbt == 0x100207e1);
}

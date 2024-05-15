
#define TEST_CC_FORMAT_LOWER 128r
#define TEST_CC_FORMAT_UPPER 128R

#include "test_common.cpp"
#include "cap_ap.h"

TEST_CASE("update sealed", "[sealed]") {
    _cc_cap_t cap;

    _cc_N(update_sealed)(&cap, 1);
    CHECK_FIELD(cap, is_sealed, 1);
}

/*
 * For 64-bit bakewell, the conversion between AP bitfield and its encoding
 * is a trivial 1:1 mapping. Some spot checks will do, there's no point in
 * checking all combinations.
 */

/* AP compression */

TEST_CASE_AP_COMP(CAP_AP_R, CAP_AP_R)

TEST_CASE_AP_COMP(
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR),
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR))

/* AP decompression */

TEST_CASE_AP_DECOMP(
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR),
        (CAP_AP_M | CAP_AP_X | CAP_AP_R | CAP_AP_W | CAP_AP_C | CAP_AP_ASR))

TEST_CASE_AP_DECOMP(
        (CAP_AP_X | CAP_AP_R | CAP_AP_W),
        (CAP_AP_X | CAP_AP_R | CAP_AP_W))

TEST_CASE("bounds encoding exponent 0", "[bounds]") {
    /* params are base, cursor, top */
    _cc_cap_t cap = CompressedCap128r::make_max_perms_cap(0x0, 0x10, 0x20);

    /*
     * EF == 1 -> exponent 0
     * { T, TE } == 0b0000_0010_0000
     * { B, BE } == 0
     * all of LCout, LMSB, c_t, c_b are 0
     *
     * top == 0x20, base == 0x0
     */
    CHECK(cap.cr_pesbt == 0xf800004080000);
}

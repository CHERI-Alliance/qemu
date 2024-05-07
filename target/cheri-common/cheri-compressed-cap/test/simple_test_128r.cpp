
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

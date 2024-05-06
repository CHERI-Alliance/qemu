
#define TEST_CC_FORMAT_LOWER 64r
#define TEST_CC_FORMAT_UPPER 64R

#include "test_common.cpp"

TEST_CASE("update sealed", "[sealed]") {
    _cc_cap_t cap;

    _cc_N(update_sealed)(&cap, 1);
    CHECK_FIELD(cap, is_sealed, 1);
}

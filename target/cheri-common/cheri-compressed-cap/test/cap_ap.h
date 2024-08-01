
#ifndef _TEST_CAP_AP_H
#define _TEST_CAP_AP_H

/*
 * At first, it looked like we could generate both test cases (AP ->
 * cr_arch_perm and cr_arch_perm -> AP) from one macro.
 * This didn't work: for error cases, the result depends on the direction.
 */

#define TEST_CASE_AP_COMP(_cr_arch_perm, _ap) \
   TEST_CASE(#_cr_arch_perm " compress", "[compress]") { \
      _cc_cap_t cap; \
      memset(&cap, 0x00, sizeof(cap)); \
\
      cap.cr_arch_perm |= (_cr_arch_perm); \
      _cc_N(ap_compress)(&cap); \
\
      CHECK(_cc_N(get_ap)(&cap) == (_ap)); \
}


#define TEST_CASE_AP_DECOMP(_ap, _cr_arch_perm) \
   TEST_CASE(#_ap " decompress", "[decompress]") { \
      _cc_cap_t cap; \
      memset(&cap, 0x00, sizeof(cap)); \
\
      _cc_N(update_ap)(&cap, _ap); \
      _cc_N(ap_decompress)(&cap); \
\
      CHECK(cap.cr_arch_perm == (_cr_arch_perm)); \
}

#endif

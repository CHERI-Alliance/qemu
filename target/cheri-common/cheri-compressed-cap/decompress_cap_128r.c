/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Alex Richardson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "cheri_compressed_cap.h"
// #include "cheri_compressed_cap.h"
#ifdef DECOMPRESS_WITH_SAIL_GENERATED_CODE
#include "test/sail_wrapper.h"
#endif

static const char* decode_ap(uint8_t ap) {
    static char result[16];
    char *p = result;

#define AP_CASE_NAME(x, y)		\
    if (ap & CAP_AP_##x)		\
	p = stpcpy(p, y);
#define AP_CASE(x) AP_CASE_NAME(x, #x)
    AP_CASE(C)
    AP_CASE(W)
    AP_CASE(R)
    AP_CASE(X)
    AP_CASE_NAME(ASR, "Asr")

    return result;
}

static void dump_cap_fields(const cc128r_cap_t* result) {
    fprintf(stderr, "SDP:         0x%" PRIx32 "\n", cc128r_get_sdp(result));
    fprintf(stderr, "M:           0x%" PRIx32 "\n", result->cr_m);
    fprintf(stderr, "AP:          0x%" PRIx32 " (%s)\n", result->cr_arch_perm, decode_ap(result->cr_arch_perm));

    fprintf(stderr, "Base:        0x%016" PRIx64 "\n", result->cr_base);
    fprintf(stderr, "Offset:      0x%016" PRIx64 "\n", result->_cr_cursor - result->cr_base);
    fprintf(stderr, "Cursor:      0x%016" PRIx64 "\n", result->_cr_cursor);
    cc128r_length_t length = result->_cr_top - result->cr_base;
    fprintf(stderr, "Length:     0x%" PRIx64 "%016" PRIx64 " %s\n", (uint64_t)(length >> 64), (uint64_t)length,
            length > UINT64_MAX ? " (greater than UINT64_MAX)" : "");
    cc128r_length_t top_full = result->_cr_top;
    fprintf(stderr, "Top:        0x%" PRIx64 "%016" PRIx64 " %s\n", (uint64_t)(top_full >> 64), (uint64_t)top_full,
            top_full > UINT64_MAX ? " (greater than UINT64_MAX)" : "");
    fprintf(stderr, "Sealed:      %d\n", cc128r_is_cap_sealed(result) ? 1 : 0);
    fprintf(stderr, "Reserved:    0x%" PRIx8 "\n", cc128r_get_reserved(result));
    fprintf(stderr, "Valid decompress: %s", result->cr_bounds_valid ? "yes" : "no");
    fprintf(stderr, "\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s PESBT CURSOR\n", argv[0]);
        return EXIT_FAILURE;
    }
    errno = 0;
    char* end;
    uint64_t pesbt = strtoull(argv[1], &end, 16);
    if (errno != 0 || !end || *end != '\0') {
        err(EX_DATAERR, "pesbt not a valid hex number: %s", argv[1]);
    }
    uint64_t cursor = strtoull(argv[2], &end, 16);
    if (errno != 0 || !end || *end != '\0') {
        err(EX_DATAERR, "cursor not a valid hex number: %s", argv[2]);
    }
    printf("Decompressing pesbt = %016" PRIx64 ", cursor = %016" PRIx64 "\n", pesbt, cursor);
#ifdef DECOMPRESS_WITH_SAIL_GENERATED_CODE
    cc128r_cap_t result = sail_decode_128r_mem(pesbt, cursor, false);
#else
    cc128r_cap_t result;
    memset(&result, 0, sizeof(result));
    cc128r_decompress_mem(pesbt, cursor, false, &result);
#endif
    dump_cap_fields(&result);
#ifdef DECOMPRESS_WITH_SAIL_GENERATED_CODE
    uint64_t rt_pesbt = sail_compress_128r_mem(&result);
#else
    uint64_t rt_pesbt = cc128r_compress_mem(&result);
#endif
    printf("Re-compressed pesbt = %016" PRIx64 "%s\n", rt_pesbt, pesbt == rt_pesbt ? "" : " - WAS DESTRUCTIVE");
    return EXIT_SUCCESS;
}

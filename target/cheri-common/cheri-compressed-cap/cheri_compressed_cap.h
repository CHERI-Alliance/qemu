/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Lawrence Esswood
 * Copyright (c) 2018-2020 Alex Richardson
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
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
#ifndef CHERI_COMPRESSED_CAP_H
#define CHERI_COMPRESSED_CAP_H

#ifdef CC_IS_MORELLO
#error "Use new cc128m/CC128M definitions instead of defining CC_IS_MORELLO"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TAG_CAUSE_INITIALISATION (0)
#define TAG_CAUSE_IS_TAGGED (1 << 31)
#define TAG_CAUSE_UNSEALED (1 << 0)
#define TAG_CAUSE_UNREPRESENTABLE (1 << 1)
#define TAG_CAUSE_BOUNDS_INVALID (1 << 2)
#define TAG_CAUSE_DECOMPRESS (1 << 3)
#define TAG_CAUSE_SEALED_TRAP_VECTOR (1 << 4)
#define TAG_CAUSE_SENTRY_MISMATCH (1 << 5)
#define TAG_CAUSE_SEALED_UNALIGNED (1 << 6)
#define TAG_CAUSE_NULL_AUTH (1 << 7)
#define TAG_CAUSE_SEAL_INVALID (1 << 8)
#define TAG_CAUSE_CLEAR_TAG (1 << 9)
#define TAG_CAUSE_PERMS (1 << 10)
#define TAG_CAUSE_NON_CANONICAL (1 << 11)
#define TAG_CAUSE_UNDEFINED (1 << 12)
#define TAG_CAUSE_INTEGER_OP (1 << 13)
#define TAG_CAUSE_DEFERRED (1 << 14)
#define TAG_CAUSE_STORE_NO_CAP_PERM (1 << 15)
#define TAG_CAUSE_STORE_LOCAL (1 << 16)
#define TAG_CAUSE_LOAD_NO_CAP_PERM (1 << 17)

// clang-format off
#include "cheri_compressed_cap_64.h"
#include "cheri_compressed_cap_64r.h"
#include "cheri_compressed_cap_128.h"
#include "cheri_compressed_cap_128m.h"
#include "cheri_compressed_cap_128r.h"
// clang-format on

/* Legacy CHERI256 things: */
#include "cheri_compressed_cap_256.h"

#endif /* CHERI_COMPRESSED_CAP_H */

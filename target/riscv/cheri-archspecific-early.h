/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 Alex Richardson <Alexander.Richardson@cl.cam.ac.uk>
 * All rights reserved.
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
#pragma once

#include "cheri_defs.h"
#include "cheri-archspecific-earlier.h"

/*
 * This controls how many tags are fetched with cgettags/csettags
 */
#ifdef TARGET_RISCV32
#define CAP_TAG_GET_MANY_SHFT 3
#else
#define CAP_TAG_GET_MANY_SHFT 2
#endif

/* Capability Exception Codes */
typedef enum CheriCapExc {
    CapEx_TagViolation                  = 0x0,
    CapEx_SealViolation                 = 0x1,
    CapEx_PermissionViolation           = 0x2,
    CapEx_AddressViolation              = 0x3,
    CapEx_LengthViolation               = 0x4,
    // Other types need to map to the ones above
    // 0xd - 0xf reserved
    CapEx_UnalignedBase                 = CapEx_AddressViolation,  // possibly not used on riscv
    CapEx_InexactBounds                 = CapEx_AddressViolation,  // should not be used as we would clear tags instead
    CapEx_UserDefViolation              = CapEx_PermissionViolation,  // looks to be permissions only used by checkperm so not riscv
    CapEx_GlobalViolation               = CapEx_PermissionViolation,
    CapEx_PermitExecuteViolation        = CapEx_PermissionViolation,
    CapEx_PermitLoadViolation           = CapEx_PermissionViolation,
    CapEx_PermitStoreViolation          = CapEx_PermissionViolation,
    CapEx_PermitLoadCapViolation        = CapEx_PermissionViolation,
    CapEx_PermitStoreCapViolation       = CapEx_PermissionViolation,
    CapEx_PermitStoreLocalCapViolation  = CapEx_PermissionViolation,
    CapEx_PermitSealViolation           = CapEx_PermissionViolation,
    CapEx_AccessSystemRegsViolation     = CapEx_PermissionViolation,
    CapEx_PermitCCallViolation          = CapEx_PermissionViolation,
    CapEx_AccessCCallIDCViolation       = CapEx_PermissionViolation, // not used
    CapEx_PermitUnsealViolation         = CapEx_PermissionViolation,
    CapEx_PermitSetCIDViolation         = CapEx_PermissionViolation,
    // 0x1d - 0x1f reserved
} CheriCapExcCause;

enum CheriSCR {
    CheriSCR_PCC = 0,
    CheriSCR_DDC = 1,

    CheriSCR_STDC = 13,

    CheriSCR_MTDC = 29,

    /*
     * TODO(am2419): Register indices are placeholders,
     * awaiting full specification of background registers.
     */
    CheriSCR_BSTCC = 32,
    CheriSCR_BSTDC = 33,
    CheriSCR_BSScratchC = 34,
    CheriSCR_BSEPCC = 35,
    CheriSCR_MAX,
};

#define CHERI_EXC_REGNUM_PCC (32 + CheriSCR_PCC)
#define CHERI_EXC_REGNUM_DDC (32 + CheriSCR_DDC)
#define CHERI_CONTROLFLOW_CHECK_AT_TARGET 0
#define CHERI_TAG_CLEAR_ON_INVALID(env) true
#define CHERI_TRANSLATE_DDC_RELOCATION(ctx) false
#define CINVOKE_DATA_REGNUM 31

static inline const cap_register_t *cheri_get_ddc(CPURISCVState *env) {
    cheri_debug_assert(env->DDC.cr_extra == CREG_FULLY_DECOMPRESSED);
    return &env->DDC;
}

static inline const cap_register_t *_cheri_get_pcc_unchecked(CPURISCVState *env)
{
    cheri_debug_assert(env->PCC.cr_extra == CREG_FULLY_DECOMPRESSED);
    return &env->PCC;
}

static inline GPCapRegs *cheri_get_gpcrs(CPUArchState *env) {
    return &env->gpcapregs;
}

#define CHERI_GPCAPREGS_MEMBER gpcapregs

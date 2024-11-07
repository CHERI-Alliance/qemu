/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Alex Richardson
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

#pragma once
#include "qemu/compiler.h"
#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "cheri_defs.h"

/* cap_set_sealed does not need a type for cheri bakewell */
#define SEALED_TYPE_UNUSED 0

#ifdef TARGET_AARCH64
#define PRINT_CAP_FMT_EXTRA " bv: %d"
#define PRINT_CAP_ARGS_EXTRA(cr) , (cr)->cr_bounds_valid
#else
#define PRINT_CAP_FMT_EXTRA
#define PRINT_CAP_ARGS_EXTRA(cr)
#endif

#ifdef TARGET_CHERI

/*
 * We take the absence of both otype and flags fields as an indication that
 * we're using one of the capability format from the RISC-V specification
 * (which might be used on other architectures in future).
 * TODO: Can we make this check more precise?
 */
#define CHERI_FMT_RISCV \
   (CAP_CC(FIELD_FLAGS_USED) == 0) && (CAP_CC(FIELD_OTYPE_USED) == 0)

#if CHERI_FMT_RISCV
/* We're using one of the cheri risc-v capability formats. */
#define PRINT_CAP_FMTSTR_L1 \
    "v:%d sdp:%1x m:%d ap:%2x ct:%d b:" TARGET_FMT_lx " a:" TARGET_FMT_lx \
    " t:" TARGET_FMT_lx
#define PRINT_CAP_ARGS_L1(cr) \
    (cr)->cr_tag, cap_get_sdp(cr), (cr)->cr_m, (cr)->cr_arch_perm, \
    !cap_is_unsealed(cr), cap_get_base(cr), cap_get_cursor(cr), cap_get_top(cr)

#define COMBINED_PERMS_VALUE(unused) 0
#define PRINT_CAP_FMTSTR_L2 "%s"
#define PRINT_CAP_ARGS_L2(unused) ""
#else
/* We're using one of the cheri v9 capability formats. */
#define PRINT_CAP_FMTSTR_L1                                                    \
    "v:%d s:%d p:%08x f:%d b:" TARGET_FMT_lx " l:" TARGET_FMT_lx
#define COMBINED_PERMS_VALUE(cr)                                               \
    (unsigned)(((cap_get_uperms(cr) & CAP_UPERMS_ALL) << CAP_UPERMS_SHFT) |    \
               (cap_get_perms(cr) & CAP_PERMS_ALL))
#define PRINT_CAP_ARGS_L1(cr)                                                  \
    (cr)->cr_tag, cap_is_sealed_with_type(cr), COMBINED_PERMS_VALUE(cr),       \
        cap_get_flags(cr), cap_get_base(cr), cap_get_length_sat(cr)
#define PRINT_CAP_FMTSTR_L2                                                    \
    "\n             |o:" TARGET_FMT_lx " t:" TARGET_FMT_lx PRINT_CAP_FMT_EXTRA
#define PRINT_CAP_ARGS_L2(cr)                                                  \
    (target_ulong) cap_get_offset(cr),                                         \
        cap_get_otype_unsigned(cr) PRINT_CAP_ARGS_EXTRA(cr)
#endif

#define PRINT_CAP_FMTSTR PRINT_CAP_FMTSTR_L1 " " PRINT_CAP_FMTSTR_L2
#define PRINT_CAP_ARGS(cr) PRINT_CAP_ARGS_L1(cr), PRINT_CAP_ARGS_L2(cr)

#define GET_HOST_RETPC() const uintptr_t _host_return_address = GETPC()


static inline target_ulong cap_get_cursor(const cap_register_t *c)
{
    return c->_cr_cursor;
}

// Getters to allow future changes to the cap_register_t struct layout:
static inline target_ulong cap_get_base(const cap_register_t *c)
{
    return c->cr_base;
}

static inline cap_offset_t cap_get_offset(const cap_register_t *c)
{
    return (cap_offset_t)c->_cr_cursor - (cap_offset_t)c->cr_base;
}

static inline uint32_t cap_get_uperms(const cap_register_t *c)
{
    return CAP_cc(get_uperms)(c);
}

static inline uint32_t cap_get_sdp(const cap_register_t *c)
{
    return CAP_cc(get_sdp)(c);
}

/*
 * qemu uses the CAP_PERMS_xxx defines in lots of places, including code
 * that'll be shared between v9 and bakewell. We keep using those defines
 * although they're based on the permissions in v9. For bakewell, we have
 * to map AP settings to CAP_PERMS_xxx.
 */
static inline uint32_t cap_get_perms(const cap_register_t *c)
{
#if CAP_CC(FIELD_HWPERMS_USED) == 1
    return CAP_cc(get_perms)(c);
#else
    uint32_t res = 0;

    /* We can safely assume that c->cr_arch_perm and c's AP field are in sync. */
    if (c->cr_arch_perm & CAP_AP_R) {
        res |= CAP_PERM_LOAD;
        /*
         * As of May 2024, bakewell has no local/global mechanism. Read or
         * write implies global.
         */
        res |= CAP_PERM_GLOBAL;
        if (c->cr_arch_perm & CAP_AP_C) {
            res |= CAP_PERM_LOAD_CAP;
        }
    }
    if (c->cr_arch_perm & CAP_AP_W) {
        res |= CAP_PERM_STORE;
        /* see above */
        res |= CAP_PERM_GLOBAL;
        /* Without local/global, store implies store local. */
        res |= CAP_PERM_STORE_LOCAL;
        if (c->cr_arch_perm & CAP_AP_C) {
            res |= CAP_PERM_STORE_CAP;
            /* Write access to a capability implies seal + unseal. */
            res |= CAP_PERM_SEAL | CAP_PERM_UNSEAL;
            /*
             * As of May 2024, bakewell has no compartment id. Write access
             * to a capability implies the right of set all its metadata.
             */
            res |= CAP_PERM_SETCID;
        }
    }
    if (c->cr_arch_perm & CAP_AP_X) {
        res |= CAP_PERM_EXECUTE;
        /* Execute implies executing via a sealed capability. */
        res |= CAP_PERM_CINVOKE;
    }
    if (c->cr_arch_perm & CAP_AP_ASR) {
        res |= CAP_ACCESS_SYS_REGS;
    }

    return res;
#endif
}

static inline uint8_t cap_get_flags(const cap_register_t *c)
{
    return CAP_cc(get_flags)(c);
}

static inline bool cap_get_capmode(const cap_register_t *c)
{
#if CAP_CC(FIELD_FLAGS_USED) == 1
    return CAP_cc(get_flags)(c);
#else
    CAP_cc(m_ap_decompress)((cap_register_t *)c);
    return !c->cr_m;
#endif
}

static inline void cap_set_capmode(cap_register_t *c, bool enable)
{
#if CAP_CC(FIELD_FLAGS_USED) == 1
    CAP_cc(update_flags)(c, enable ? 1 : 0);
#else
    CAP_cc(m_ap_decompress)(c);
    if (enable && c->cr_m == 1) {
        c->cr_m = 0;
        CAP_cc(m_ap_compress)(c);
    }
    else if (!enable && c->cr_m == 0) {
        c->cr_m = 1;
        CAP_cc(m_ap_compress)(c);
    }
#endif
}


static inline bool cap_has_reserved_bits_set(const cap_register_t *c)
{
    return (CAP_cc(get_reserved)(c) != 0) || (CAP_cc(get_reserved2)(c) != 0);
}

// The top of the capability (exclusive -- i.e., one past the end)
static inline target_ulong cap_get_top(const cap_register_t *c)
{
    // TODO: should handle last byte of address space properly
    cap_length_t top = c->_cr_top;
    return top > ~(target_ulong)0 ? ~(target_ulong)0 : (target_ulong)top;
}

static inline cap_length_t cap_get_length_full(const cap_register_t *c)
{
    return c->_cr_top - c->cr_base;
}

static inline target_ulong cap_get_length_sat(const cap_register_t *c)
{
#ifndef TARGET_AARCH64
    cheri_debug_assert((!c->cr_tag || c->_cr_top >= c->cr_base) &&
                       "Tagged capabilities must be in bounds!");
#endif
    cap_length_t length = cap_get_length_full(c);
    // Clamp the length to ~(target_ulong)0
    return length > ~(target_ulong)0 ? ~(target_ulong)0 : (target_ulong)length;
}

static inline cap_length_t cap_get_top_full(const cap_register_t *c)
{
    return c->_cr_top;
}

static inline bool cap_otype_is_reserved(target_ulong otype)
{
    cheri_debug_assert(otype <= CAP_MAX_REPRESENTABLE_OTYPE &&
                       "Should only be called for in-range otypes!");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
    return otype >= CAP_CC(MIN_RESERVED_OTYPE) &&
           otype <= CAP_CC(MAX_RESERVED_OTYPE);
#pragma GCC diagnostic pop
}

static inline target_ulong cap_get_otype_unsigned(const cap_register_t *c)
{
    target_ulong otype = CAP_cc(get_otype)(c);
    /*
     * It is impossible to have out-of-range otypes in all targets for the
     * currently used capability compression schemes.
     */
    cheri_debug_assert(otype <= CAP_MAX_REPRESENTABLE_OTYPE);
    return otype;
}

static inline target_long cap_get_otype_signext(const cap_register_t *c)
{
    target_ulong result = cap_get_otype_unsigned(c);
    if (result > CAP_MAX_REPRESENTABLE_OTYPE) {
        // raw bits loaded from memory
        assert(!c->cr_tag && "Capabilities with otype > max cannot be tagged!");
        return result;
    }
#ifdef TARGET_AARCH64
    /*
     * Morello does not sign extend like the rest of CHERI.
     */
    return result;
#else
    /*
     * We "sign" extend to a 64-bit number by subtracting the maximum:
     * e.g. for 64-bit CHERI-RISC-V unsigned 2^18-1 maps to 2^64-1
     */
    return result < CAP_CC(MIN_RESERVED_OTYPE)
               ? result
               : result - CAP_MAX_REPRESENTABLE_OTYPE - 1;
#endif
}

static inline bool cap_exactly_equal(const cap_register_t *cbp,
                                     const cap_register_t *ctp)
{
    return CAP_cc(exactly_equal)(cbp, ctp);
}

/*
 * This function is called only from helpers for v9 instructions. We do not
 * need a bakewell version.
 */
static inline bool cap_is_sealed_with_reserved_otype(const cap_register_t *c)
{
    target_ulong otype = cap_get_otype_unsigned(c);
    return cap_otype_is_reserved(otype) && otype != CAP_OTYPE_UNSEALED;
}

#if CAP_CC(FIELD_OTYPE_USED) == 1
static inline bool cap_is_sealed_with_type(const cap_register_t *c)
{
    /* cap_otype_is_reserved() returns true for unsealed capabilities. */
    return !cap_otype_is_reserved(cap_get_otype_unsigned(c));
}

static inline bool cap_is_unsealed(const cap_register_t *c)
{
    target_ulong otype = cap_get_otype_unsigned(c);
    return otype == CAP_OTYPE_UNSEALED;
}

static inline void cap_set_sealed(cap_register_t *c, uint32_t type)
{
    assert(c->cr_tag);
    assert(cap_is_unsealed(c) && "Should only use this with unsealed caps");
    assert(!cap_otype_is_reserved(type) &&
           "Can't use this to set reserved otypes");
    CAP_cc(update_otype)(c, type);
}

static inline void cap_set_unsealed(cap_register_t *c)
{
    assert(c->cr_tag);
    assert(cap_is_sealed_with_type(c) &&
           "should not use this to unseal reserved types");
    CAP_cc(update_otype)(c, CAP_OTYPE_UNSEALED);
}

static inline bool cap_is_sealed_entry(const cap_register_t *c)
{
    return cap_get_otype_unsigned(c) == CAP_OTYPE_SENTRY;
}

static inline void cap_unseal_reserved_otype(cap_register_t *c)
{
    assert(c->cr_tag && cap_is_sealed_with_reserved_otype(c) &&
           "Should only be used with reserved object types");
    CAP_cc(update_otype)(c, CAP_OTYPE_UNSEALED);
}

static inline void cap_unseal_entry(cap_register_t *c)
{
    assert(c->cr_tag && cap_is_sealed_entry(c) &&
           "Should only be used with sentry capabilities");
    CAP_cc(update_otype)(c, CAP_OTYPE_UNSEALED);
}

static inline void cap_make_sealed_entry(cap_register_t *c)
{
    assert(c->cr_tag && cap_is_unsealed(c) &&
           "Should only be used with unsealed capabilities");
    CAP_cc(update_otype)(c, CAP_OTYPE_SENTRY);
}

#else

/* If the otype field is not used, we have bakewell's sealed bit. */
static inline bool cap_is_unsealed(const cap_register_t *c)
{
    return !CAP_cc(get_ct)(c);
}

static inline bool cap_is_sealed_with_type(const cap_register_t *c)
{
    /*
     * Cheri bakewell has no concept of "sealed with a specific otype".
     *
     * For bakewell, the only real user is cheri_jump_and_link_checked()
     * else if (cap_is_sealed_with_type(target) || ...) {
     *    ... throw "Seal Violation" exception
     *
     * If we defined cap_is_sealed_with_type to be equivalent to "capability
     * is sealed", we'd get a seal violation exception for every jump that
     * uses a sealed capability.
     *
     * Let's return false here and disable the special case in the check above.
     */
    return false;
}

static inline void cap_set_sealed(cap_register_t *c, uint32_t type)
{
    CAP_cc(update_ct)(c, 1);
}

static inline void cap_set_unsealed(cap_register_t *c)
{
    CAP_cc(update_ct)(c, 0);
}

static inline bool cap_is_sealed_entry(const cap_register_t *c)
{
    /* Cheri bakewell does not distinguish between sealed and sealed entry. */
    return !cap_is_unsealed(c);
}

static inline void cap_unseal_reserved_otype(cap_register_t *c)
{
    cap_set_unsealed(c);
}

static inline void cap_unseal_entry(cap_register_t *c)
{
    cap_set_unsealed(c);
}

static inline void cap_make_sealed_entry(cap_register_t *c)
{
    cap_set_sealed(c, SEALED_TYPE_UNUSED);
}
#endif

/*
 * Check if cr_m, cr_arch_perm contain a valid set of risc-v cheri
 * mode and architectural permissions that could have been produced
 * by acperm.
 */
static inline bool valid_m_ap(uint8_t cr_m, uint8_t cr_arch_perm)
{
    /* "ASR permission cannot be set without X permission" */
    if ((cr_arch_perm & (CAP_AP_ASR | CAP_AP_X)) == CAP_AP_ASR) {
        return false;
    }
    /*
     * "C-permission cannot be set without at least one of R-permission or
     * W-permission being set."
     */
    if ((cr_arch_perm & (CAP_AP_C | CAP_AP_R | CAP_AP_W)) == CAP_AP_C) {
        return false;
    }

    /*
     * We were considering a check for M=1 (int ptr mode) in purecap mode.
     * This check would be incorrect. acperm could produce a capability with
     * M=1 in purecap mode.
     *
     * acperm does not require that cs1 be tagged. If in purecap mode, we pass
     * cs1 with tag=0, M=1, AP=X and rs2 = 0x1F to acperm, cd will have M=1.
     * (cd's tag would still be cleared, cbld would refuse to tag such a
     * capability).
     */

    /*
     * Please see the comment in CHERI_HELPER_IMPL(acperm).
     * In a non-executable capability, M is undefined and must be set to 0.
     */
    if (cr_m && !(cr_arch_perm & CAP_AP_X)) {
        return false;
    }

#if CAP_CC(ADDR_WIDTH) == 32
    /* ASR requires that all one other permissions be set. */
    if (cr_arch_perm & CAP_AP_ASR) {
        if ((cr_arch_perm & (CAP_AP_C | CAP_AP_R | CAP_AP_W | CAP_AP_X)) !=
                (CAP_AP_C | CAP_AP_R | CAP_AP_W | CAP_AP_X)) {
            return false;
        }
    }
    /* If R is not set, C and X must not be set either. */
    if (!(cr_arch_perm & CAP_AP_R)) {
        if (cr_arch_perm & (CAP_AP_C | CAP_AP_X)) {
            return false;
        }
    }
    /* It's an error if X and R are set, but W and C aren't. */
    if ((cr_arch_perm & (CAP_AP_C | CAP_AP_W | CAP_AP_R | CAP_AP_X)) ==
            (CAP_AP_X | CAP_AP_R)) {
        return false;
    }
#endif

    return true;
}

// Check if num_bytes bytes at addr can be read using capability c
static inline bool cap_is_in_bounds(const cap_register_t *c, target_ulong addr,
                                    size_t num_bytes)
{
#ifdef TARGET_AARCH64
    // Invalid exponent caps are always considered out of bounds.
    if (!c->cr_bounds_valid)
        return false;
#endif
    if (addr < cap_get_base(c)) {
        return false;
    }
    /*
     * Use __builtin_add_overflow to detect avoid wrapping around the end of
     * the address space. However, we have to be careful to allow accesses to
     * the last byte (wrapping to exactly zero) since that is fine when
     * checking against given an omnipotent capability.
     */
    target_ulong access_end_addr = 0;
    if (unlikely(__builtin_add_overflow(addr, num_bytes, &access_end_addr))) {
        /* Only do the extended precision addition if we do overflow. */
        if (cap_get_top_full(c) >= (cap_length_t)addr + num_bytes) {
            return true;
        }
        if (c->cr_tag)
            warn_report("Found capability access that wraps around: 0x" TARGET_FMT_lx
                        " + %zd. Authorizing cap: " PRINT_CAP_FMTSTR,
                        addr, num_bytes, PRINT_CAP_ARGS(c));
        return false;
    }
    if (access_end_addr > cap_get_top_full(c)) {
        return false;
    }
    return true;
}

static inline QEMU_ALWAYS_INLINE bool
addr_in_cap_bounds(const cap_register_t *c, target_ulong addr)
{
    return addr >= cap_get_base(c) && addr < cap_get_top_full(c);
}

static inline QEMU_ALWAYS_INLINE bool
cap_cursor_in_bounds(const cap_register_t *c)
{
    return addr_in_cap_bounds(c, cap_get_cursor(c));
}

static inline QEMU_ALWAYS_INLINE bool cap_has_perms(const cap_register_t *reg,
                                                    uint32_t perms)
{
    return (cap_get_perms(reg) & perms) == perms;
}

static inline bool cap_is_representable(const cap_register_t *c)
{
    return CAP_cc(is_representable_cap_exact)(c);
}

static inline void assert_valid_jump_target(const cap_register_t *target)
{
    // All of these properties should have been checked in the helper:
    cheri_debug_assert(cap_is_unsealed(target));
    cheri_debug_assert(cap_has_perms(target, CAP_PERM_EXECUTE));
    cheri_debug_assert(target->cr_tag);
    cheri_debug_assert(cap_cursor_in_bounds(target));
}

static inline cap_register_t *null_capability(cap_register_t *cp)
{
    *cp = CAP_cc(make_null_derived_cap(0));
    cp->cr_extra = CREG_FULLY_DECOMPRESSED;
    return cp;
}

static inline bool is_null_capability(const cap_register_t *cp)
{
    cap_register_t null;
    // This also compares padding but it should always be NULL assuming
    // null_capability() was used
    return memcmp(null_capability(&null), cp, sizeof(cap_register_t)) == 0;
}

/*
 * Convert 64-bit integer into a capability that holds the integer in
 * its offset field.
 *
 *       cap.base = 0, cap.tag = false, cap.offset = x
 *
 * The contents of other fields of int to cap depends on the capability
 * compression scheme in use (e.g. 256-bit capabilities or 128-bit
 * compressed capabilities). In particular, with 128-bit compressed
 * capabilities, length is not always zero. The length of a capability
 * created via int to cap is not semantically meaningful, and programs
 * should not rely on it having any particular value.
 */
static inline const cap_register_t *int_to_cap(target_ulong x,
                                               cap_register_t *cr)
{

    (void)null_capability(cr);
    cr->_cr_cursor = x;
    return cr;
}

/*
 * Clear the tag bit of a capability that became unrepresentable and update
 * the cursor to point to @p addr.
 * Only clearing the tag bit instead of updating the offset to be semantically
 * valid improves timing on the FPGA and with the shift towards address-based
 * interpretation it becomes less useful to retain the offset value.
 *
 * Previous behaviour was to use int_to_cap instead
 *
 */
static inline cap_register_t *cap_mark_unrepresentable(target_ulong addr,
                                                       cap_register_t *cr)
{
    // Clear the tag and update the address:
    cr->_cr_cursor = addr;
    cr->cr_tag = false;
    /*
     * Recompute the decompressed bounds relative to the new address. In most
     * cases they will refer to a different region of memory now.
     */
    CAP_cc(decompress_raw)(cr->cr_pesbt, addr, false, cr);
    cr->cr_extra = CREG_FULLY_DECOMPRESSED;
    return cr;
}

/*
 * attribute unused marks a potentially unused paramter. We keep it to squelch
 * compiler warnings for architectures other than risc-v.
 */
static inline void set_max_perms_capability(__attribute__((unused)) CPUArchState *env,
        cap_register_t *crp, target_ulong cursor)
{
    bool m = false;

#if defined(TARGET_RISCV)
    /*
     * If hybrid mode is supported, the infinite capability has to set integer
     * pointer mode (M = 1).
     */
    m = riscv_feature(env, RISCV_FEATURE_CHERI_HYBRID);
#endif
    *crp = CAP_cc(make_max_perms_cap_m_lv)(0, cursor, CAP_MAX_TOP, m, 0);
    crp->cr_extra = CREG_FULLY_DECOMPRESSED;
}

static inline QEMU_ALWAYS_INLINE bool
is_representable_cap_with_addr(const cap_register_t *cap, target_ulong new_addr)
{
    return CAP_cc(is_representable_with_addr)(
        cap, new_addr, /*precise_representable_check=*/true);
}

int gdb_get_capreg(GByteArray *buf, const cap_register_t *cap);
int gdb_get_general_purpose_capreg(GByteArray *buf, CPUArchState *env,
                                   unsigned regnum);

#define raise_cheri_exception(env, cause, type, reg)                           \
    raise_cheri_exception_impl(env, cause, type, reg, 0, true,                 \
                               _host_return_address)

#define raise_cheri_exception_addr(env, cause, type, reg, addr)                \
    raise_cheri_exception_impl(env, cause, type, reg, addr, true,              \
                               _host_return_address)

#ifdef TARGET_AARCH64
#define raise_cheri_exception_if(env, cause, type, addr, reg)                  \
    raise_cheri_exception_impl_if_wnr(env, cause, type, reg, addr, true,       \
                                      /*pc=*/0, true, false)
#define raise_cheri_exception_addr_wnr(env, cause, type, reg, addr, is_write)  \
    raise_cheri_exception_impl_if_wnr(env, cause, type, reg, addr, true,       \
                                      _host_return_address, false, is_write)
#else
#define raise_cheri_exception_if(env, cause, type, addr, reg)                  \
    raise_cheri_exception_impl(env, cause, type, reg, addr, true, /*pc=*/0)
#define raise_cheri_exception_addr_wnr(env, cause, type, reg, addr, is_write)  \
    raise_cheri_exception_addr(env, cause, type, reg, addr)
#endif

static inline void cap_set_cursor(cap_register_t *cap, uint64_t new_addr)
{
    if (!is_representable_cap_with_addr(cap, new_addr)) {
        cap_mark_unrepresentable(new_addr, cap);
    } else {
        cap->_cr_cursor = new_addr;
    }
}

static inline void cap_increment_offset(cap_register_t *cap, uint64_t offset)
{
    uint64_t new_addr = cap->_cr_cursor + offset;
    return cap_set_cursor(cap, new_addr);
}

#endif /* TARGET_CHERI */

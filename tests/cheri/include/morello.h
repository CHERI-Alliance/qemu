/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Morello inline assembly primitives and MMU helpers for bare-metal C tests.
 * Stock clang does not know Morello mnemonics, so they are emitted with .inst.
 * Note: This file was AI-generated for the tests/cheri regression suite.
 */
#ifndef TESTS_CHERI_MORELLO_H
#define TESTS_CHERI_MORELLO_H

#include "common.h"

/* Descriptor attributes: AF, inner shareable, MAIR index 0, SC, LC=01 */
#define MORELLO_PTE_ATTRS       ((1ULL << 10) | (3ULL << 8) | \
                                 (1ULL << 60) | (1ULL << 61))
#define MORELLO_PTE_ATTRS_NO_SC ((1ULL << 10) | (3ULL << 8) | (1ULL << 61))
#define MORELLO_PTE_AP_RO       (1ULL << 7)
#define MORELLO_TCR_EL1         ((25ULL << 0) | (1ULL << 8) | (1ULL << 10) | \
                                 (3ULL << 12) | (1ULL << 23) | \
                                 (1ULL << 41) | (0xfULL << 43))

asm(
    ".ifndef MORELLO_MACROS_DEFINED\n\t"
    ".set MORELLO_MACROS_DEFINED, 1\n\t"
    ".macro MRS_DDC ct\n\t"
    ".inst 0xc29b4120 | (\\ct)\n\t"
    ".endm\n\t"
    ".macro MSR_CVBAR_EL1 ct\n\t"
    ".inst 0xc288c000 | (\\ct)\n\t"
    ".endm\n\t"
    ".macro SCVALUE cd, cn, xm\n\t"
    ".inst 0xc2c04000 | ((\\xm) << 16) | ((\\cn) << 5) | (\\cd)\n\t"
    ".endm\n\t"
    ".macro SCBNDS cd, cn, xm\n\t"
    ".inst 0xc2c00000 | ((\\xm) << 16) | ((\\cn) << 5) | (\\cd)\n\t"
    ".endm\n\t"
    ".macro GCTAG xd, cn\n\t"
    ".inst 0xc2c09000 | ((\\cn) << 5) | (\\xd)\n\t"
    ".endm\n\t"
    ".macro STR_CAP ct, xn, off=0\n\t"
    ".inst 0xc2000000 | (((\\off) / 16) << 10) | ((\\xn) << 5) | (\\ct)\n\t"
    ".endm\n\t"
    ".macro LDR_CAP ct, xn, off=0\n\t"
    ".inst 0xc2400000 | (((\\off) / 16) << 10) | ((\\xn) << 5) | (\\ct)\n\t"
    ".endm\n\t"
    ".macro SWP_CAP cs, ct, xn\n\t"
    ".inst 0xa2208000 | ((\\cs) << 16) | ((\\xn) << 5) | (\\ct)\n\t"
    ".endm\n\t"
    ".macro LDXR_CAP ct, xn\n\t"
    ".inst 0x225f7c00 | ((\\xn) << 5) | (\\ct)\n\t"
    ".endm\n\t"
    ".macro STXR_CAP ws, ct, xn\n\t"
    ".inst 0x22007c00 | ((\\ws) << 16) | ((\\xn) << 5) | (\\ct)\n\t"
    ".endm\n\t"
    ".macro LDXP_CAP ct1, ct2, xn\n\t"
    ".inst 0x227f0000 | ((\\ct2) << 10) | ((\\xn) << 5) | (\\ct1)\n\t"
    ".endm\n\t"
    ".macro STXP_CAP ws, ct1, ct2, xn\n\t"
    ".inst 0x22200000 | ((\\ws) << 16) | ((\\ct2) << 10) | ((\\xn) << 5) | (\\ct1)\n\t"
    ".endm\n\t"
    ".macro STP_CAP ct1, ct2, xn, off=0\n\t"
    ".inst 0x42800000 | ((((\\off) / 16) & 0x7f) << 15) | ((\\ct2) << 10) | "
    "((\\xn) << 5) | (\\ct1)\n\t"
    ".endm\n\t"
    ".endif\n\t"
);

extern volatile uint64_t morello_fault_count;
int64_t morello_cpu_on(unsigned long cpu_id);
bool on_sync_exception(uint64_t esr, uint64_t elr);

static inline void morello_enable_mmu(const uint64_t *l1_table)
{
    uint64_t mair = 0xff;
    uint64_t tcr = MORELLO_TCR_EL1;
    asm volatile(
        "dsb sy\n\t"
        "msr mair_el1, %0\n\t"
        "msr tcr_el1, %1\n\t"
        "msr ttbr0_el1, %2\n\t"
        "isb\n\t"
        "mrs %0, sctlr_el1\n\t"
        "orr %0, %0, #1\n\t"
        "msr sctlr_el1, %0\n\t"
        "isb\n\t"
        : "+r"(mair)
        : "r"(tcr), "r"(l1_table)
        : "memory");
}

static inline uint64_t morello_load_u64(const void *addr)
{
    uint64_t val = 0;
    asm volatile("ldr %0, [%1]" : "+r"(val) : "r"(addr) : "memory");
    return val;
}

static inline void morello_store_u64(void *addr, uint64_t val)
{
    asm volatile("str %1, [%0]" :: "r"(addr), "r"(val) : "memory");
}

static inline void morello_store_u32(void *addr, uint32_t val)
{
    asm volatile("str %w1, [%0]" :: "r"(addr), "r"(val) : "memory");
}

static inline void morello_store_pair_u64(void *addr, uint64_t v1, uint64_t v2)
{
    asm volatile("stp %1, %2, [%0]"
                 :: "r"(addr), "r"(v1), "r"(v2) : "memory");
}

static inline void morello_store_q128(void *addr, uint64_t lo, uint64_t hi)
{
    asm volatile(
        "fmov d0, %1\n\t"
        "mov  v0.d[1], %2\n\t"
        "str  q0, [%0]\n\t"
        :: "r"(addr), "r"(lo), "r"(hi) : "v0", "memory");
}

static inline void morello_store_null_cap(void *slot)
{
    register void *x0 asm("x0") = slot;
    asm volatile("STR_CAP 31, 0, 0" :: "r"(x0) : "memory");
}

static inline void morello_store_ddc_addr(void *slot, uintptr_t addr)
{
    register void *x0 asm("x0") = slot;
    register uintptr_t x1 asm("x1") = addr;
    asm volatile(
        "MRS_DDC 2\n\t"
        "SCVALUE 2, 2, 1\n\t"
        "STR_CAP 2, 0, 0\n\t"
        :: "r"(x0), "r"(x1) : "x2", "memory");
}

static inline void morello_store_ddc_cap(void *slot, uintptr_t base, size_t len)
{
    register void *x0 asm("x0") = slot;
    register uintptr_t x1 asm("x1") = base;
    register size_t x2 asm("x2") = len;
    asm volatile(
        "MRS_DDC 3\n\t"
        "SCVALUE 3, 3, 1\n\t"
        "SCBNDS  3, 3, 2\n\t"
        "STR_CAP 3, 0, 0\n\t"
        :: "r"(x0), "r"(x1), "r"(x2) : "x3", "memory");
}

static inline void morello_stp_cap_pair(void *pair_slot,
                                        const void *cap1_slot,
                                        const void *cap2_slot)
{
    register void *x0 asm("x0") = pair_slot;
    register const void *x1 asm("x1") = cap1_slot;
    register const void *x2 asm("x2") = cap2_slot;
    asm volatile(
        "LDR_CAP 3, 1, 0\n\t"
        "LDR_CAP 4, 2, 0\n\t"
        "STP_CAP 3, 4, 0, 0\n\t"
        :: "r"(x0), "r"(x1), "r"(x2) : "x3", "x4", "memory");
}

static inline struct cap_info morello_load_cap(const void *slot)
{
    register const void *x0 asm("x0") = slot;
    register uintptr_t addr asm("x1");
    register uintptr_t tag asm("x2");
    asm volatile(
        "LDR_CAP 3, 0, 0\n\t"
        "GCTAG   2, 3\n\t"
        "mov     x1, x3\n\t"
        : "=r"(addr), "=r"(tag)
        : "r"(x0)
        : "x3", "memory");
    return (struct cap_info){ .addr = addr, .tag = (tag != 0) };
}

static inline bool morello_ldxr_stxr_inc_cursor(void *slot, long max_retries)
{
    register void *x0 asm("x0") = slot;
    register long retries asm("x1") = max_retries;
    register uintptr_t status asm("x4");
    asm volatile(
        "1:\n\t"
        "LDXR_CAP 2, 0\n\t"
        "add      x3, x2, #1\n\t"
        "SCVALUE  2, 2, 3\n\t"
        "STXR_CAP 4, 2, 0\n\t"
        "cbz      w4, 2f\n\t"
        "subs     x1, x1, #1\n\t"
        "b.ne     1b\n\t"
        "2:\n\t"
        : "+r"(retries), "=r"(status)
        : "r"(x0)
        : "x2", "x3", "cc", "memory");
    return (uint32_t)status == 0;
}

static inline bool morello_treiber_push(void *stack_top,
                                        const void *node_cap_slot,
                                        long max_retries)
{
    register void *x0 asm("x0") = stack_top;
    register const void *x1 asm("x1") = node_cap_slot;
    register long retries asm("x5") = max_retries;
    register uintptr_t status asm("x6");
    asm volatile(
        "LDR_CAP  4, 1, 0\n\t"
        "1:\n\t"
        "LDR_CAP  2, 0, 0\n\t"
        "STR_CAP  2, 4, 0\n\t"
        "dmb      ish\n\t"
        "LDXR_CAP 3, 0\n\t"
        "cmp      x3, x2\n\t"
        "b.ne     2f\n\t"
        "STXR_CAP 6, 4, 0\n\t"
        "cbz      w6, 3f\n\t"
        "2:\n\t"
        "subs     x5, x5, #1\n\t"
        "b.ne     1b\n\t"
        "mov      w6, #1\n\t"
        "3:\n\t"
        : "+r"(retries), "=r"(status)
        : "r"(x0), "r"(x1)
        : "x2", "x3", "x4", "cc", "memory");
    return (uint32_t)status == 0;
}

static inline struct stack_node *morello_treiber_pop(void *stack_top,
                                                     void *out_cap_slot,
                                                     bool *tagged_out,
                                                     long max_retries)
{
    register void *x0 asm("x0") = stack_top;
    register void *x1 asm("x1") = out_cap_slot;
    register long retries asm("x3") = max_retries;
    register uintptr_t popped asm("x4");
    register uintptr_t tag asm("x5");
    register uintptr_t status asm("x6");
    asm volatile(
        "1:\n\t"
        "LDXR_CAP 4, 0\n\t"
        "GCTAG    5, 4\n\t"
        "cbz      x5, 2f\n\t"
        "LDR_CAP  2, 4, 0\n\t"
        "STXR_CAP 6, 2, 0\n\t"
        "cbz      w6, 2f\n\t"
        "subs     x3, x3, #1\n\t"
        "b.ne     1b\n\t"
        "b        3f\n\t"
        "2:\n\t"
        "STR_CAP  4, 1, 0\n\t"
        "3:\n\t"
        : "+r"(retries), "=r"(popped), "=r"(tag), "=r"(status)
        : "r"(x0), "r"(x1)
        : "x2", "cc", "memory");
    *tagged_out = (tag != 0);
    if (!*tagged_out || (uint32_t)status != 0) {
        return NULL;
    }
    return (struct stack_node *)popped;
}

static inline uint32_t morello_aba_pop_race(void *stack_top,
                                            volatile uint32_t *aba_state)
{
    register void *x0 asm("x0") = stack_top;
    register volatile uint32_t *x1 asm("x1") = aba_state;
    register uintptr_t status asm("x4");
    asm volatile(
        "LDXR_CAP 2, 0\n\t"
        "LDR_CAP  3, 2, 0\n\t"
        "dsb      ish\n\t"
        "ldar     w4, [x1]\n\t"
        "cmp      w4, #2\n\t"
        "b.eq     2f\n\t"
        "mov      w4, #1\n\t"
        "stlr     w4, [x1]\n\t"
        "1:\n\t"
        "ldar     w4, [x1]\n\t"
        "cmp      w4, #2\n\t"
        "b.ne     1b\n\t"
        "2:\n\t"
        "STXR_CAP 4, 3, 0\n\t"
        : "=r"(status)
        : "r"(x0), "r"(x1)
        : "x2", "x3", "cc", "memory");
    return (uint32_t)status;
}

static inline uint32_t morello_aba_ldxp_stxp_race(void *pair_slot,
                                                  const void *new_cap_slot,
                                                  volatile uint32_t *aba_state)
{
    register void *x0 asm("x0") = pair_slot;
    register const void *x1 asm("x1") = new_cap_slot;
    register volatile uint32_t *x2 asm("x2") = aba_state;
    register uintptr_t status asm("x4");
    asm volatile(
        "LDR_CAP  6, 1, 0\n\t"
        "LDXP_CAP 3, 5, 0\n\t"
        "dsb      ish\n\t"
        "ldar     w4, [x2]\n\t"
        "cmp      w4, #4\n\t"
        "b.eq     2f\n\t"
        "mov      w4, #3\n\t"
        "stlr     w4, [x2]\n\t"
        "1:\n\t"
        "ldar     w4, [x2]\n\t"
        "cmp      w4, #4\n\t"
        "b.ne     1b\n\t"
        "2:\n\t"
        "STXP_CAP 4, 6, 6, 0\n\t"
        : "=r"(status)
        : "r"(x0), "r"(x1), "r"(x2)
        : "x3", "x5", "x6", "cc", "memory");
    return (uint32_t)status;
}

#endif /* TESTS_CHERI_MORELLO_H */

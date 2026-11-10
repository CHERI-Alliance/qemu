/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * RVY (RISC-V CHERI 0.9.10) inline assembly primitives for bare-metal C tests.
 * Stock clang does not know the RVY mnemonics, so they are emitted with .insn.
 * Note: This file was AI-generated for the tests/cheri regression suite.
 */
#ifndef TESTS_CHERI_RVY_H
#define TESTS_CHERI_RVY_H

#include "common.h"

asm(
    ".macro YADDRW cd, cs1, rs2\n\t"
    ".insn r 0x7b, 0, 0x0b, \\cd, \\cs1, \\rs2\n\t"
    ".endm\n\t"
    ".macro YBNDSW cd, cs1, rs2\n\t"
    ".insn r 0x7b, 0, 0x1b, \\cd, \\cs1, \\rs2\n\t"
    ".endm\n\t"
    ".macro YBASER rd, cs1\n\t"
    ".insn r 0x7b, 0, 0x7a, \\rd, \\cs1, x0\n\t"
    ".endm\n\t"
    ".macro YLENR rd, cs1\n\t"
    ".insn r 0x7b, 0, 0x7a, \\rd, \\cs1, x3\n\t"
    ".endm\n\t"
    ".macro YTAGR rd, cs1\n\t"
    ".insn r 0x7b, 0, 0x7a, \\rd, \\cs1, x4\n\t"
    ".endm\n\t"
    ".macro LY cd, addr\n\t"
    ".insn i 0x7b, 1, \\cd, \\addr\n\t"
    ".endm\n\t"
    ".macro SY cs2, addr\n\t"
    ".insn s 0x7b, 2, \\cs2, \\addr\n\t"
    ".endm\n\t"
    ".macro AMOSWAP_Y cd, cs1, cs2\n\t"
    ".insn r 0x7b, 3, 0x04, \\cd, \\cs1, \\cs2\n\t"
    ".endm\n\t"
    ".macro LR_Y cd, cs1\n\t"
    ".insn r 0x7b, 3, 0x08, \\cd, \\cs1, x0\n\t"
    ".endm\n\t"
    ".macro SC_Y rd, cs1, cs2\n\t"
    ".insn r 0x7b, 3, 0x0c, \\rd, \\cs1, \\cs2\n\t"
    ".endm\n\t"
    ".macro CBO_ZERO rs1\n\t"
    ".insn i 0x0f, 2, x0, 4(\\rs1)\n\t"
    ".endm\n\t"
);

static inline void rvy_cbo_zero(void *addr)
{
    asm volatile("CBO_ZERO %0" :: "r"(addr) : "memory");
}

static inline uintptr_t rvy_mhartid(void)
{
    uintptr_t id;
    asm volatile("csrr %0, mhartid" : "=r"(id));
    return id;
}

static inline void rvy_store_null_cap(void *slot)
{
    asm volatile("SY zero, 0(%0)" :: "r"(slot) : "memory");
}

static inline void rvy_store_ddc_addr(void *slot, uintptr_t addr)
{
    uintptr_t tmp;
    asm volatile(
        "csrrs  %0, 0x03f, x0\n\t"
        "YADDRW %0, %0, %2\n\t"
        "SY     %0, 0(%1)\n\t"
        : "=&r"(tmp)
        : "r"(slot), "r"(addr)
        : "memory");
}

static inline void rvy_store_ddc_cap(void *slot, uintptr_t base,
                                     size_t len, uintptr_t cursor)
{
    uintptr_t tmp;
    asm volatile(
        "csrrs  %0, 0x03f, x0\n\t"
        "YADDRW %0, %0, %2\n\t"
        "YBNDSW %0, %0, %3\n\t"
        "YADDRW %0, %0, %4\n\t"
        "SY     %0, 0(%1)\n\t"
        : "=&r"(tmp)
        : "r"(slot), "r"(base), "r"(len), "r"(cursor)
        : "memory");
}

static inline bool rvy_load_cap_tag(const void *slot, uintptr_t *addr_out)
{
    uintptr_t cap, tag;
    asm volatile(
        "LY    %0, 0(%2)\n\t"
        "YTAGR %1, %0\n\t"
        : "=&r"(cap), "=r"(tag)
        : "r"(slot)
        : "memory");
    if (addr_out) {
        *addr_out = cap;
    }
    return tag != 0;
}

static inline struct cap_info rvy_load_cap(const void *slot)
{
    struct cap_info info;
    uintptr_t tag;
    asm volatile(
        "LY     %0, 0(%4)\n\t"
        "YBASER %1, %0\n\t"
        "YLENR  %2, %0\n\t"
        "YTAGR  %3, %0\n\t"
        : "=&r"(info.addr), "=&r"(info.base), "=&r"(info.len), "=r"(tag)
        : "r"(slot)
        : "memory");
    info.tag = (tag != 0);
    return info;
}

static inline uintptr_t rvy_amoswap_untagged(void *slot, uintptr_t val)
{
    uintptr_t old;
    asm volatile("AMOSWAP_Y %0, %1, %2"
                 : "=r"(old)
                 : "r"(slot), "r"(val)
                 : "memory");
    return old;
}

static inline bool rvy_lrsc_inc_cursor(void *slot, long max_retries)
{
    uintptr_t cap, next, status;
    asm volatile(
        "1:\n\t"
        "LR_Y   %0, %4\n\t"
        "addi   %1, %0, 1\n\t"
        "YADDRW %1, %0, %1\n\t"
        "SC_Y   %2, %4, %1\n\t"
        "beqz   %2, 2f\n\t"
        "addi   %3, %3, -1\n\t"
        "bnez   %3, 1b\n\t"
        "2:\n\t"
        : "=&r"(cap), "=&r"(next), "=&r"(status), "+r"(max_retries)
        : "r"(slot)
        : "memory");
    return status == 0;
}

static inline bool rvy_treiber_push(void *stack_top, const void *node_cap_slot,
                                    long max_retries)
{
    uintptr_t node_cap, old_top, cur_top, status;
    asm volatile(
        "LY     %0, 0(%5)\n\t"
        "1:\n\t"
        "LY     %1, 0(%4)\n\t"
        "SY     %1, 0(%0)\n\t"
        "fence  rw, rw\n\t"
        "LR_Y   %2, %4\n\t"
        "bne    %2, %1, 2f\n\t"
        "SC_Y   %3, %4, %0\n\t"
        "beqz   %3, 3f\n\t"
        "2:\n\t"
        "addi   %6, %6, -1\n\t"
        "bnez   %6, 1b\n\t"
        "li     %3, 1\n\t"
        "3:\n\t"
        : "=&r"(node_cap), "=&r"(old_top), "=&r"(cur_top), "=&r"(status),
          "+r"(stack_top), "+r"(node_cap_slot), "+r"(max_retries)
        :
        : "memory");
    return status == 0;
}

static inline struct stack_node *rvy_treiber_pop(void *stack_top,
                                                 void *out_cap_slot,
                                                 bool *tagged_out,
                                                 long max_retries)
{
    uintptr_t popped, next, tag, status;
    asm volatile(
        "1:\n\t"
        "LR_Y   %0, %5\n\t"
        "YTAGR  %2, %0\n\t"
        "beqz   %2, 2f\n\t"
        "LY     %1, 0(%0)\n\t"
        "SC_Y   %3, %5, %1\n\t"
        "beqz   %3, 2f\n\t"
        "addi   %4, %4, -1\n\t"
        "bnez   %4, 1b\n\t"
        "j      3f\n\t"
        "2:\n\t"
        "SY     %0, 0(%6)\n\t"
        "3:\n\t"
        : "=&r"(popped), "=&r"(next), "=&r"(tag), "=&r"(status),
          "+r"(max_retries)
        : "r"(stack_top), "r"(out_cap_slot)
        : "memory");
    *tagged_out = (tag != 0);
    if (!*tagged_out || status != 0) {
        return NULL;
    }
    return (struct stack_node *)popped;
}

/*
 * Hart 0 side of the Treiber stack ABA test:
 *   - Starts pop() with LR.Y on stack_top (reading node_A) and LY on
 *     node_A->next (reading node_B).
 *   - Sets *aba_state = 3 and spins until Hart 1 sets *aba_state = 4 after
 *     popping A, popping/poisoning B, and pushing A back.
 *   - Executes SC.Y of stale node_B to stack_top and returns the SC.Y result
 *     (non-zero means SC.Y detected the ABA modification and failed).
 */
static inline uintptr_t rvy_aba_pop_race(void *stack_top,
                                         volatile uint32_t *aba_state)
{
    uintptr_t top, next, tmp, status;
    asm volatile(
        "LR_Y   %0, %4\n\t"
        "LY     %1, 0(%0)\n\t"
        "fence  rw, rw\n\t"
        "li     %3, 4\n\t"
        "lw     %2, 0(%5)\n\t"
        "beq    %2, %3, 2f\n\t"
        "li     %2, 3\n\t"
        "sw     %2, 0(%5)\n\t"
        "1:\n\t"
        "lw     %2, 0(%5)\n\t"
        "bne    %2, %3, 1b\n\t"
        "2:\n\t"
        "fence  r, rw\n\t"
        "SC_Y   %3, %4, %1\n\t"
        : "=&r"(top), "=&r"(next), "=&r"(tmp), "=&r"(status)
        : "r"(stack_top), "r"(aba_state)
        : "memory");
    return status;
}

static inline void rvy_alternate_sy_until_stop(void *slot,
                                               const void *cap_a_slot,
                                               const void *cap_b_slot,
                                               const volatile uint32_t *stop)
{
    uintptr_t cap_a, cap_b, flag;
    asm volatile(
        "LY     %0, 0(%4)\n\t"
        "LY     %1, 0(%5)\n\t"
        "1:\n\t"
        "SY     %0, 0(%3)\n\t"
        "SY     %1, 0(%3)\n\t"
        "lw     %2, 0(%6)\n\t"
        "beqz   %2, 1b\n\t"
        : "=&r"(cap_a), "=&r"(cap_b), "=&r"(flag)
        : "r"(slot), "r"(cap_a_slot), "r"(cap_b_slot), "r"(stop)
        : "memory");
}

#endif /* TESTS_CHERI_RVY_H */

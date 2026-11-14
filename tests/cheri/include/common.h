/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Common freestanding definitions for bare-metal CHERI regression tests.
 * Note: This test infrastructure was AI-generated to expose MTTCG capability
 * tag memory bugs.
 */
#ifndef TESTS_CHERI_COMMON_H
#define TESTS_CHERI_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE  4096
#define ALIGNED(n) __attribute__((aligned(n)))
#define NORETURN   __attribute__((noreturn))
#define WEAK       __attribute__((weak))

/* 16-byte capability storage slot in memory. */
struct cap_slot {
    uint64_t words[2];
} ALIGNED(16);

/* Decoded capability fields returned by cap load helpers. */
struct cap_info {
    uintptr_t addr;
    uintptr_t base;
    size_t len;
    bool tag;
};

/*
 * 32-byte lock-free Treiber stack node:
 *   +0:  next pointer capability (16 bytes)
 *   +16: node ID or pop counter (8 bytes)
 *   +24: padding to 32-byte capability bounds (8 bytes)
 */
struct stack_node {
    struct cap_slot next;
    uint64_t count;
    uint64_t pad;
} ALIGNED(32);

#define atomic_load_acquire(ptr) \
    __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_store_release(ptr, val) \
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE)
#define atomic_fetch_add_relaxed(ptr, val) \
    __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED)
#define atomic_fetch_add_seq_cst(ptr, val) \
    __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_xor_seq_cst(ptr, val) \
    __atomic_fetch_xor(ptr, val, __ATOMIC_SEQ_CST)

static inline void barrier_wait(uint32_t *bar, uint32_t ncpus)
{
    atomic_fetch_add_seq_cst(bar, 1);
    while (atomic_load_acquire(bar) != ncpus) {
    }
}

void exit_test(int code) NORETURN;
int test_main(void);
void secondary_main(unsigned long cpu_id);

static inline void check(bool cond, int fail_code)
{
    if (!cond) {
        exit_test(fail_code);
    }
}

#endif /* TESTS_CHERI_COMMON_H */

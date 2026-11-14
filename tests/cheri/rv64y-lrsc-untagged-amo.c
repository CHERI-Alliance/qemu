/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Exercises four LR/SC and AMO cases relevant to page-granular seqlock tag
 * schemes:
 * - Lock-free Treiber stack ABA detection with LR.Y/SC.Y: Hart 0 starts
 *   pop() with LR.Y on stack_top (observing node_A with next = node_B),
 *   Hart 1 pops node_A, pops and poisons node_B, and pushes node_A back
 *   (stack_top -> node_A -> node_C), and Hart 0's SC.Y must fail rather
 *   than installing the freed node_B as stack_top.
 * - Four-hart LR.Y/SC.Y contention on a single capability counter (catches
 *   livelock when a failed SC increments the page sequence number).
 * - Four-hart concurrent lock-free Treiber stack push/pop stress test
 *   verifying no lost/duplicated nodes, capability tags, or pop counts.
 * - Four-hart scalar AMOs, LR.D/SC.D, and untagged AMOSWAP.Y on an untagged
 *   ivshmem-plain PCI BAR (ram->cheri_tags == NULL).
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 contended LR.Y/SC.Y livelocked, 2 contended
 * LR.Y/SC.Y lost updates or tag, 3 concurrent Treiber stack livelocked,
 * 4 concurrent Treiber stack corrupted nodes or counts, 5 ivshmem BAR
 * unexpectedly kept a capability tag, 6 untagged-RAM AMO lost updates,
 * 7 Treiber stack LR.Y/SC.Y missed ABA or corrupted stack.
 */
#include "rvy.h"

#define NHARTS         4
#define ITERS_PER_HART 2000
#define STACK_ITERS    500
#define MAX_SC_RETRIES 2000000
#define NNODES         (NHARTS + 2)
#define PCIE_ECAM_DEV1 0x30008000UL
#define UNTAGGED_BAR   0x40000000UL

static struct {
    struct cap_slot stack_top;
    uint8_t pad0[48];
    struct stack_node aba_nodes[3];
    uint8_t pad1[96];
    struct stack_node stress_nodes[NNODES];
} lrsc_page ALIGNED(PAGE_SIZE);

/*
 * Keep per-hart capability scratch slots and barriers on a separate page so
 * local scratch updates never touch lrsc_page's CheriTagBlock seqlock.
 */
static struct {
    volatile uint32_t aba_state;
    struct cap_slot hart_node_caps[NNODES];
    struct cap_slot aba_cap_a;
    struct cap_slot aba_cap_b;
    struct cap_slot aba_cap_c;
    uint32_t bar_a1;
    uint32_t bar_a2;
    uint32_t bar_b1;
    uint32_t bar_b2;
    uint32_t bar_c1;
    uint32_t bar_c2;
} sync_page ALIGNED(PAGE_SIZE);

static void run_treiber_aba_hart0(void)
{
    struct stack_node *node_a = &lrsc_page.aba_nodes[0];
    struct stack_node *node_b = &lrsc_page.aba_nodes[1];
    struct stack_node *node_c = &lrsc_page.aba_nodes[2];

    /* Build initial stack: stack_top -> node_A -> node_B -> node_C -> NULL. */
    rvy_store_ddc_cap(&sync_page.aba_cap_c, (uintptr_t)node_c,
                      sizeof(*node_c), (uintptr_t)node_c);
    rvy_store_null_cap(&node_c->next);
    node_c->count = 0xc3;

    rvy_store_ddc_cap(&sync_page.aba_cap_b, (uintptr_t)node_b,
                      sizeof(*node_b), (uintptr_t)node_b);
    rvy_store_ddc_cap(&node_b->next, (uintptr_t)node_c,
                      sizeof(*node_c), (uintptr_t)node_c);
    node_b->count = 0xb2;

    rvy_store_ddc_cap(&sync_page.aba_cap_a, (uintptr_t)node_a,
                      sizeof(*node_a), (uintptr_t)node_a);
    rvy_store_ddc_cap(&node_a->next, (uintptr_t)node_b,
                      sizeof(*node_b), (uintptr_t)node_b);
    node_a->count = 0xa1;

    rvy_store_ddc_cap(&lrsc_page.stack_top, (uintptr_t)node_a,
                      sizeof(*node_a), (uintptr_t)node_a);
    atomic_store_release(&sync_page.aba_state, 1);

    while (atomic_load_acquire(&sync_page.aba_state) != 2) {
    }

    uintptr_t sc_failed = rvy_aba_pop_race(&lrsc_page.stack_top,
                                           &sync_page.aba_state);
    check(sc_failed != 0, 7);

    /* Retry pop(): must pop node_A and leave stack_top == node_C. */
    bool tagged = false;
    struct stack_node *popped = rvy_treiber_pop(&lrsc_page.stack_top,
                                                &sync_page.aba_cap_a,
                                                &tagged, MAX_SC_RETRIES);
    check(popped != NULL && tagged && popped->count == 0xa1, 7);

    struct cap_info rem = rvy_load_cap(&lrsc_page.stack_top);
    check(rem.tag && ((struct stack_node *)rem.addr)->count == 0xc3, 7);

    /* Reinitialize stack_top with a 4 KiB capability for Phase A. */
    rvy_store_ddc_cap(&lrsc_page.stack_top, (uintptr_t)&lrsc_page,
                      PAGE_SIZE, (uintptr_t)&lrsc_page);
}

static void run_treiber_aba_hart1(void)
{
    while (atomic_load_acquire(&sync_page.aba_state) != 1) {
    }
    atomic_store_release(&sync_page.aba_state, 2);

    while (atomic_load_acquire(&sync_page.aba_state) != 3) {
    }

    bool tagged_a = false, tagged_b = false;
    struct stack_node *a = rvy_treiber_pop(&lrsc_page.stack_top,
                                           &sync_page.aba_cap_a,
                                           &tagged_a, MAX_SC_RETRIES);
    struct stack_node *b = rvy_treiber_pop(&lrsc_page.stack_top,
                                           &sync_page.aba_cap_b,
                                           &tagged_b, MAX_SC_RETRIES);
    check(a != NULL && b != NULL && tagged_a && tagged_b, 7);

    /* Poison freed node_B and push node_A back onto stack_top. */
    rvy_store_null_cap(&b->next);
    b->count = 0xdead;
    check(rvy_treiber_push(&lrsc_page.stack_top, &sync_page.aba_cap_a,
                           MAX_SC_RETRIES), 7);

    atomic_store_release(&sync_page.aba_state, 4);
}

static void run_lrsc_contention_phase(unsigned long hartid)
{
    barrier_wait(&sync_page.bar_a1, NHARTS);

    for (int i = 0; i < ITERS_PER_HART; i++) {
        check(rvy_lrsc_inc_cursor(&lrsc_page.stack_top, MAX_SC_RETRIES), 1);
    }

    barrier_wait(&sync_page.bar_a2, NHARTS);
    if (hartid != 0) {
        return;
    }

    struct cap_info final_cap = rvy_load_cap(&lrsc_page.stack_top);
    uintptr_t base = (uintptr_t)&lrsc_page;
    check(final_cap.tag, 2);
    check(final_cap.base == base && final_cap.len == PAGE_SIZE, 2);
    check(final_cap.addr == base + NHARTS * ITERS_PER_HART, 2);
}

static void run_treiber_stress_phase(unsigned long hartid)
{
    if (hartid == 0) {
        for (int i = 0; i < NNODES; i++) {
            struct stack_node *n = &lrsc_page.stress_nodes[i];
            rvy_store_null_cap(&n->next);
            n->count = 0;
            rvy_store_ddc_cap(&sync_page.hart_node_caps[i], (uintptr_t)n,
                              sizeof(*n), (uintptr_t)n);
        }
        /* Link nodes 4 -> 5 -> NULL onto stack_top initially. */
        struct stack_node *n4 = &lrsc_page.stress_nodes[NHARTS];
        struct stack_node *n5 = &lrsc_page.stress_nodes[NHARTS + 1];
        rvy_store_null_cap(&n5->next);
        rvy_store_ddc_cap(&n4->next, (uintptr_t)n5, sizeof(*n5), (uintptr_t)n5);
        rvy_store_ddc_cap(&lrsc_page.stack_top, (uintptr_t)n4,
                          sizeof(*n4), (uintptr_t)n4);
    }

    barrier_wait(&sync_page.bar_b1, NHARTS);

    struct cap_slot *my_cap = &sync_page.hart_node_caps[hartid];
    for (int i = 0; i < STACK_ITERS; i++) {
        check(rvy_treiber_push(&lrsc_page.stack_top, my_cap, MAX_SC_RETRIES),
              3);
        bool tagged = false;
        struct stack_node *popped = rvy_treiber_pop(&lrsc_page.stack_top,
                                                    my_cap, &tagged,
                                                    MAX_SC_RETRIES);
        check(popped != NULL, tagged ? 3 : 4);
        popped->count++;
    }

    barrier_wait(&sync_page.bar_b2, NHARTS);
    if (hartid != 0) {
        return;
    }

    /* Pop the 2 remaining nodes from stack_top into slots 4 and 5. */
    bool tagged4 = false, tagged5 = false;
    struct stack_node *rem4 = rvy_treiber_pop(&lrsc_page.stack_top,
                                              &sync_page.hart_node_caps[4],
                                              &tagged4, MAX_SC_RETRIES);
    struct stack_node *rem5 = rvy_treiber_pop(&lrsc_page.stack_top,
                                              &sync_page.hart_node_caps[5],
                                              &tagged5, MAX_SC_RETRIES);
    check(rem4 != NULL && rem5 != NULL && tagged4 && tagged5, 4);
    check(rvy_load_cap(&lrsc_page.stack_top).addr == 0, 4);

    uint32_t seen_mask = 0;
    uint64_t total_pops = 0;
    uintptr_t nodes_base = (uintptr_t)&lrsc_page.stress_nodes[0];

    for (int i = 0; i < NNODES; i++) {
        struct cap_info ci = rvy_load_cap(&sync_page.hart_node_caps[i]);
        check(ci.tag && ci.len == sizeof(struct stack_node), 4);
        struct stack_node *n = (struct stack_node *)ci.addr;
        total_pops += n->count;
        size_t idx = (ci.addr - nodes_base) / sizeof(struct stack_node);
        check(idx < NNODES, 4);
        seen_mask |= (1U << idx);
    }
    check(seen_mask == ((1U << NNODES) - 1U), 4);
    check(total_pops == (uint64_t)(NHARTS * STACK_ITERS), 4);
}

static void run_untagged_bar_phase(unsigned long hartid)
{
    uint64_t *amo64_ptr = (uint64_t *)(UNTAGGED_BAR + 16);
    uint32_t *amo32_ptr = (uint32_t *)(UNTAGGED_BAR + 24);
    uint64_t *lrsc64_ptr = (uint64_t *)(UNTAGGED_BAR + 32);
    void *amoswap_cap_ptr = (void *)(UNTAGGED_BAR + 48);

    if (hartid == 0) {
        /* Map ivshmem-plain PCI 00:01.0 BAR2 to UNTAGGED_BAR and enable MEM. */
        volatile uint32_t *bar2 = (volatile uint32_t *)(PCIE_ECAM_DEV1 + 0x18);
        volatile uint16_t *cmd = (volatile uint16_t *)(PCIE_ECAM_DEV1 + 0x04);
        bar2[0] = (uint32_t)UNTAGGED_BAR;
        bar2[1] = 0;
        *cmd = 2;
        asm volatile("fence iorw, iorw" ::: "memory");

        /* Untagged RAM clears tags on SY while storing the payload bytes. */
        rvy_store_ddc_addr((void *)UNTAGGED_BAR, UNTAGGED_BAR);
        uintptr_t addr = 0;
        bool tagged = rvy_load_cap_tag((const void *)UNTAGGED_BAR, &addr);
        check(!tagged && addr == UNTAGGED_BAR, 5);

        *amo64_ptr = 0;
        *amo32_ptr = 0;
        *lrsc64_ptr = 0;
        ((uint64_t *)amoswap_cap_ptr)[0] = 0;
        ((uint64_t *)amoswap_cap_ptr)[1] = 0;
    }

    barrier_wait(&sync_page.bar_c1, NHARTS);

    long retries = MAX_SC_RETRIES;
    for (int i = 0; i < ITERS_PER_HART; i++) {
        atomic_fetch_add_relaxed(amo64_ptr, 1ULL);
        atomic_fetch_add_relaxed(amo32_ptr, 1U);

        uint64_t tmp, status;
        asm volatile(
            "1:\n\t"
            "lr.d %0, (%3)\n\t"
            "addi %0, %0, 1\n\t"
            "sc.d %1, %0, (%3)\n\t"
            "beqz %1, 2f\n\t"
            "addi %2, %2, -1\n\t"
            "bnez %2, 1b\n\t"
            "2:\n\t"
            : "=&r"(tmp), "=&r"(status), "+r"(retries)
            : "r"(lrsc64_ptr)
            : "memory");
        check(status == 0, 6);

        (void)rvy_amoswap_untagged(amoswap_cap_ptr, hartid + 1);
    }

    barrier_wait(&sync_page.bar_c2, NHARTS);
    if (hartid == 0) {
        uint64_t expected = NHARTS * ITERS_PER_HART;
        check(*amo64_ptr == expected, 6);
        check(*amo32_ptr == (uint32_t)expected, 6);
        check(*lrsc64_ptr == expected, 6);
    }
}

static void hart_main(unsigned long hartid)
{
    if (hartid >= NHARTS) {
        return;
    }
    if (hartid == 0) {
        run_treiber_aba_hart0();
    } else if (hartid == 1) {
        run_treiber_aba_hart1();
    }
    run_lrsc_contention_phase(hartid);
    run_treiber_stress_phase(hartid);
    run_untagged_bar_phase(hartid);
}

void secondary_main(unsigned long hartid)
{
    hart_main(hartid);
}

int test_main(void)
{
    hart_main(0);
    return 0;
}

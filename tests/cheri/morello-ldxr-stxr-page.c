/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Verifies Morello load-exclusive / store-exclusive behavior across two CPUs:
 * - Lock-free Treiber stack ABA detection with LDXR/STXR: CPU 0 begins pop()
 *   with LDXR on stack_top (observing node_A with next = node_B), CPU 1 pops
 *   node_A, pops and poisons node_B, and pushes node_A back (stack_top ->
 *   node_A -> node_C), and CPU 0's STXR must fail rather than installing the
 *   freed node_B as stack_top.
 * - Capability-pair LDXP/STXP ABA detection when CPU 1 modifies a 32-byte
 *   capability pair and restores the original pair before CPU 0's STXP.
 * - Two CPUs contending on LDXR/STXR must not livelock due to failed STXRs
 *   bumping a page sequence number, and a two-CPU concurrent lock-free
 *   Treiber stack push/pop stress loop must preserve all nodes, capability
 *   tags, and pop counts.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 Treiber stack LDXR/STXR missed ABA or corrupted
 * stack, 2 LDXP/STXP missed ABA, 6 contended SMP LDXR/STXR livelocked,
 * 7 contended SMP sum/tag wrong, 8 concurrent Treiber stack livelocked,
 * 9 concurrent Treiber stack corrupted nodes or counts.
 */
#include "morello.h"

#define SMP_ITERS   2000
#define STACK_ITERS 500
#define MAX_RETRIES 1000000
#define NNODES      4

static struct {
    struct cap_slot stack_top;
    uint8_t pad0[48];
    struct stack_node aba_nodes[3];
    uint8_t pad1[96];
    struct cap_slot pair_slot[2];
    struct stack_node stress_nodes[NNODES];
} excl_page ALIGNED(PAGE_SIZE);

/*
 * Keep per-CPU capability scratch slots and synchronization flags on a
 * separate page from excl_page so scratch writes do not perturb excl_page's
 * CheriTagBlock seqlock.
 */
static struct {
    volatile uint32_t aba_state;
    struct cap_slot node_caps[NNODES];
    struct cap_slot aba_cap_a;
    struct cap_slot aba_cap_b;
    struct cap_slot aba_cap_c;
    uint32_t cpu1_ready;
    uint32_t phase1_go;
    uint32_t cpu1_phase1_done;
    uint32_t phase2_go;
    uint32_t cpu1_phase2_done;
} sync_page ALIGNED(PAGE_SIZE);

static void run_treiber_stress_loop(struct cap_slot *my_cap)
{
    for (int i = 0; i < STACK_ITERS; i++) {
        check(morello_treiber_push(&excl_page.stack_top, my_cap, MAX_RETRIES),
              8);
        bool tagged = false;
        struct stack_node *popped = morello_treiber_pop(&excl_page.stack_top,
                                                        my_cap, &tagged,
                                                        MAX_RETRIES);
        check(popped != NULL, tagged ? 8 : 9);
        popped->count++;
    }
}

void secondary_main(unsigned long cpu_id)
{
    (void)cpu_id;
    atomic_store_release(&sync_page.cpu1_ready, 1);

    /* Phase 0a: Treiber stack LDXR/STXR ABA race. */
    while (atomic_load_acquire(&sync_page.aba_state) != 1) {
    }

    bool tagged_a = false, tagged_b = false;
    struct stack_node *a = morello_treiber_pop(&excl_page.stack_top,
                                               &sync_page.aba_cap_a,
                                               &tagged_a, MAX_RETRIES);
    struct stack_node *b = morello_treiber_pop(&excl_page.stack_top,
                                               &sync_page.aba_cap_b,
                                               &tagged_b, MAX_RETRIES);
    check(a != NULL && b != NULL && tagged_a && tagged_b, 1);

    morello_store_null_cap(&b->next);
    b->count = 0xdead;
    check(morello_treiber_push(&excl_page.stack_top, &sync_page.aba_cap_a,
                               MAX_RETRIES), 1);
    atomic_store_release(&sync_page.aba_state, 2);

    /* Phase 0b: LDXP/STXP capability-pair ABA race. */
    while (atomic_load_acquire(&sync_page.aba_state) != 3) {
    }
    morello_stp_cap_pair(excl_page.pair_slot, &sync_page.aba_cap_c,
                         &sync_page.aba_cap_c);
    morello_stp_cap_pair(excl_page.pair_slot, &sync_page.aba_cap_a,
                         &sync_page.aba_cap_b);
    atomic_store_release(&sync_page.aba_state, 4);

    /* Phase 1: Contended LDXR/STXR counter increment. */
    while (!atomic_load_acquire(&sync_page.phase1_go)) {
    }
    for (int i = 0; i < SMP_ITERS; i++) {
        check(morello_ldxr_stxr_inc_cursor(&excl_page.stack_top, MAX_RETRIES),
              6);
    }
    atomic_store_release(&sync_page.cpu1_phase1_done, 1);

    /* Phase 2: Concurrent lock-free Treiber stack stress test. */
    while (!atomic_load_acquire(&sync_page.phase2_go)) {
    }
    run_treiber_stress_loop(&sync_page.node_caps[1]);
    atomic_store_release(&sync_page.cpu1_phase2_done, 1);
}

int test_main(void)
{
    /*
     * Phase 0a: Lock-free Treiber stack ABA test with LDXR/STXR.
     * Initial stack: stack_top -> node_A -> node_B -> node_C -> NULL.
     */
    struct stack_node *node_a = &excl_page.aba_nodes[0];
    struct stack_node *node_b = &excl_page.aba_nodes[1];
    struct stack_node *node_c = &excl_page.aba_nodes[2];

    morello_store_ddc_cap(&sync_page.aba_cap_c, (uintptr_t)node_c,
                          sizeof(*node_c));
    morello_store_null_cap(&node_c->next);
    node_c->count = 0xc3;

    morello_store_ddc_cap(&sync_page.aba_cap_b, (uintptr_t)node_b,
                          sizeof(*node_b));
    morello_store_ddc_cap(&node_b->next, (uintptr_t)node_c, sizeof(*node_c));
    node_b->count = 0xb2;

    morello_store_ddc_cap(&sync_page.aba_cap_a, (uintptr_t)node_a,
                          sizeof(*node_a));
    morello_store_ddc_cap(&node_a->next, (uintptr_t)node_b, sizeof(*node_b));
    node_a->count = 0xa1;

    morello_store_ddc_cap(&excl_page.stack_top, (uintptr_t)node_a,
                          sizeof(*node_a));
    morello_stp_cap_pair(excl_page.pair_slot, &sync_page.aba_cap_a,
                         &sync_page.aba_cap_b);

    if (morello_cpu_on(1) != 0) {
        return 0;
    }
    while (!atomic_load_acquire(&sync_page.cpu1_ready)) {
    }

    uint32_t stxr_failed = morello_aba_pop_race(&excl_page.stack_top,
                                                &sync_page.aba_state);
    check(stxr_failed != 0, 1);

    /* Retry pop(): must pop node_A and leave stack_top == node_C. */
    bool tagged = false;
    struct stack_node *popped = morello_treiber_pop(&excl_page.stack_top,
                                                    &sync_page.aba_cap_a,
                                                    &tagged, MAX_RETRIES);
    check(popped != NULL && tagged && popped->count == 0xa1, 1);

    struct cap_info rem = morello_load_cap(&excl_page.stack_top);
    check(rem.tag && ((struct stack_node *)rem.addr)->count == 0xc3, 1);

    /* Phase 0b: LDXP/STXP capability-pair ABA test on pair_slot. */
    uint32_t stxp_failed = morello_aba_ldxp_stxp_race(excl_page.pair_slot,
                                                      &sync_page.aba_cap_c,
                                                      &sync_page.aba_state);
    check(stxp_failed != 0, 2);

    /* Phase 1: Contended LDXR/STXR counter increment. */
    uintptr_t base = (uintptr_t)&excl_page.stack_top;
    morello_store_ddc_addr(&excl_page.stack_top, base);
    atomic_store_release(&sync_page.phase1_go, 1);

    for (int i = 0; i < SMP_ITERS; i++) {
        check(morello_ldxr_stxr_inc_cursor(&excl_page.stack_top, MAX_RETRIES),
              6);
    }
    while (!atomic_load_acquire(&sync_page.cpu1_phase1_done)) {
    }

    struct cap_info final_cap = morello_load_cap(&excl_page.stack_top);
    check(final_cap.tag && final_cap.addr == base + 2 * SMP_ITERS, 7);

    /* Phase 2: Concurrent lock-free Treiber stack push/pop stress test. */
    for (int i = 0; i < NNODES; i++) {
        struct stack_node *n = &excl_page.stress_nodes[i];
        morello_store_null_cap(&n->next);
        n->count = 0;
        morello_store_ddc_cap(&sync_page.node_caps[i], (uintptr_t)n,
                              sizeof(*n));
    }
    struct stack_node *n2 = &excl_page.stress_nodes[2];
    struct stack_node *n3 = &excl_page.stress_nodes[3];
    morello_store_null_cap(&n3->next);
    morello_store_ddc_cap(&n2->next, (uintptr_t)n3, sizeof(*n3));
    morello_store_ddc_cap(&excl_page.stack_top, (uintptr_t)n2, sizeof(*n2));

    atomic_store_release(&sync_page.phase2_go, 1);
    run_treiber_stress_loop(&sync_page.node_caps[0]);

    while (!atomic_load_acquire(&sync_page.cpu1_phase2_done)) {
    }

    bool tagged2 = false, tagged3 = false;
    struct stack_node *rem2 = morello_treiber_pop(&excl_page.stack_top,
                                                  &sync_page.node_caps[2],
                                                  &tagged2, MAX_RETRIES);
    struct stack_node *rem3 = morello_treiber_pop(&excl_page.stack_top,
                                                  &sync_page.node_caps[3],
                                                  &tagged3, MAX_RETRIES);
    check(rem2 != NULL && rem3 != NULL && tagged2 && tagged3, 9);
    check(morello_load_cap(&excl_page.stack_top).addr == 0, 9);

    uint32_t seen_mask = 0;
    uint64_t total_pops = 0;
    uintptr_t nodes_base = (uintptr_t)&excl_page.stress_nodes[0];

    for (int i = 0; i < NNODES; i++) {
        struct cap_info ci = morello_load_cap(&sync_page.node_caps[i]);
        check(ci.tag, 9);
        struct stack_node *n = (struct stack_node *)ci.addr;
        total_pops += n->count;
        size_t idx = (ci.addr - nodes_base) / sizeof(struct stack_node);
        check(idx < NNODES, 9);
        seen_mask |= (1U << idx);
    }
    check(seen_mask == ((1U << NNODES) - 1U), 9);
    check(total_pops == (uint64_t)(2 * STACK_ITERS), 9);
    return 0;
}

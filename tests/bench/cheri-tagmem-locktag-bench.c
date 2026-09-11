/*
 * Host-only throughput microbenchmark for CHERI's per-capability tag lock
 * (target/cheri-common/cheri_tagmem_locktag.h), included directly so this
 * measures the real primitive rather than a copy of it. No QEMU/TCG/guest
 * machinery is involved: this isolates the lock/data-structure choice from
 * every other confound (JIT, TLB, translation cache, host scheduler noise
 * from the rest of QEMU), so it can A/B a candidate change to the locking
 * scheme in seconds. Correctness (no torn reads, clean release state) is
 * already covered by tests/unit/test-cheri-tagmem-locktag.c; this only
 * measures throughput, on the same acquire/release shapes real callers use
 * (see cheri_tagmem.c): a writer acquires without setting the tag, does its
 * work, then releases while committing the new tag; a reader acquires
 * (which also returns the tag), reads, then releases.
 *
 * The -s/--slots option controls contention shape: 1 slot means every
 * thread contends on the same lock (worst case); many slots spreads threads
 * across independent locks (best case), the two extremes the real workload
 * sits somewhere between depending on how much distinct tagged memory is
 * actually being touched concurrently.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or
 * later. See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/host-utils.h"
#include "qemu/processor.h"
#include "qemu/memalign.h"
#include "../../target/cheri-common/cheri_tagmem_locktag.h"

struct thread_info {
    uint64_t rng_state;
    uint64_t ops;
} QEMU_ALIGNED(64);

struct tag_slot {
    lock_tag lock;
} QEMU_ALIGNED(64);

static QemuThread *reader_threads;
static QemuThread *writer_threads;
static struct thread_info *reader_info;
static struct thread_info *writer_info;
static unsigned int n_readers = 2;
static unsigned int n_writers = 2;
static unsigned int n_ready_threads;
static struct tag_slot *slots;
static unsigned int n_slots = 1;
static unsigned int duration = 1;
static bool test_start;
static bool test_stop;

static const char commands_string[] =
    " -r = number of reader threads\n"
    " -w = number of writer threads\n"
    " -s = number of independent lock slots (rounded up to pow2;\n"
    "      1 = maximum contention, all threads share one lock)\n"
    " -d = duration in seconds";

static void usage_complete(char *argv[])
{
    fprintf(stderr, "Usage: %s [options]\n", argv[0]);
    fprintf(stderr, "options:\n%s\n", commands_string);
}

/* From: https://en.wikipedia.org/wiki/Xorshift */
static uint64_t xorshift64star(uint64_t x)
{
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * UINT64_C(2685821657736338717);
}

static void *writer_thread_func(void *arg)
{
    struct thread_info *info = arg;
    uint64_t state = info->rng_state;
    uint64_t ops = 0;

    qatomic_inc(&n_ready_threads);
    while (!qatomic_read(&test_start)) {
        cpu_relax();
    }

    while (!qatomic_read(&test_stop)) {
        state = xorshift64star(state);
        unsigned int idx = state & (n_slots - 1);
        bool tag = state & 1;

        lock_tag_write_acquire_lock(&slots[idx].lock);
        lock_tag_write_tag_and_release(&slots[idx].lock, tag);
        ops++;
    }

    info->ops = ops;
    return NULL;
}

static void *reader_thread_func(void *arg)
{
    struct thread_info *info = arg;
    uint64_t state = info->rng_state;
    uint64_t ops = 0;

    qatomic_inc(&n_ready_threads);
    while (!qatomic_read(&test_start)) {
        cpu_relax();
    }

    while (!qatomic_read(&test_stop)) {
        state = xorshift64star(state);
        unsigned int idx = state & (n_slots - 1);

        lock_tag_read_tag_and_acquire_lock(&slots[idx].lock);
        lock_tag_release_read(&slots[idx].lock);
        ops++;
    }

    info->ops = ops;
    return NULL;
}

static void run_test(void)
{
    unsigned int i;
    unsigned int n_total_threads = n_readers + n_writers;

    while (qatomic_read(&n_ready_threads) != n_total_threads) {
        cpu_relax();
    }

    qatomic_set(&test_start, true);
    g_usleep(duration * G_USEC_PER_SEC);
    qatomic_set(&test_stop, true);

    for (i = 0; i < n_readers; i++) {
        qemu_thread_join(&reader_threads[i]);
    }
    for (i = 0; i < n_writers; i++) {
        qemu_thread_join(&writer_threads[i]);
    }
}

static void create_threads(void)
{
    unsigned int i;

    slots = qemu_memalign(64, sizeof(*slots) * n_slots);
    memset(slots, 0, sizeof(*slots) * n_slots);

    reader_threads = g_new(QemuThread, n_readers);
    reader_info = g_new0(struct thread_info, n_readers);
    for (i = 0; i < n_readers; i++) {
        reader_info[i].rng_state = (i + 1) ^ time(NULL);
        qemu_thread_create(&reader_threads[i], NULL, reader_thread_func,
                           &reader_info[i], QEMU_THREAD_JOINABLE);
    }

    writer_threads = g_new(QemuThread, n_writers);
    writer_info = g_new0(struct thread_info, n_writers);
    for (i = 0; i < n_writers; i++) {
        writer_info[i].rng_state = (i + 1 + n_readers) ^ time(NULL);
        qemu_thread_create(&writer_threads[i], NULL, writer_thread_func,
                           &writer_info[i], QEMU_THREAD_JOINABLE);
    }
}

static void pr_params(void)
{
    printf("Parameters:\n");
    printf(" # of readers:       %u\n", n_readers);
    printf(" # of writers:       %u\n", n_writers);
    printf(" # of lock slots:    %u\n", n_slots);
    printf(" duration:           %u\n", duration);
}

static void pr_stats(void)
{
    unsigned long long read_ops = 0, write_ops = 0;
    unsigned int i;

    for (i = 0; i < n_readers; i++) {
        read_ops += reader_info[i].ops;
    }
    for (i = 0; i < n_writers; i++) {
        write_ops += writer_info[i].ops;
    }

    double read_mops = read_ops / (double)duration / 1e6;
    double write_mops = write_ops / (double)duration / 1e6;

    printf("Results:\n");
    printf(" Read throughput:    %.2f Mops/s (%.2f Mops/s/thread)\n",
           read_mops, n_readers ? read_mops / n_readers : 0.0);
    printf(" Write throughput:   %.2f Mops/s (%.2f Mops/s/thread)\n",
           write_mops, n_writers ? write_mops / n_writers : 0.0);
    printf(" Combined:           %.2f Mops/s\n", read_mops + write_mops);
}

static void parse_args(int argc, char *argv[])
{
    int c;

    for (;;) {
        c = getopt(argc, argv, "hd:r:w:s:");
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'h':
            usage_complete(argv);
            exit(0);
        case 'd':
            duration = atoi(optarg);
            break;
        case 'r':
            n_readers = atoi(optarg);
            break;
        case 'w':
            n_writers = atoi(optarg);
            break;
        case 's':
            n_slots = pow2ceil(atoi(optarg));
            break;
        }
    }

    if (n_slots == 0) {
        n_slots = 1;
    }
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    pr_params();
    create_threads();
    run_test();
    pr_stats();
    return 0;
}

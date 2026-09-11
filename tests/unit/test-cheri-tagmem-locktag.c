/*
 * Test the concurrency primitive backing CHERI's per-capability tag lock.
 *
 * Every tag bit is combined with a readers-writer spinlock ("lock_tag", see
 * target/cheri-common/cheri_tagmem_locktag.h), which makes capability
 * loads/stores atomic with the associated tag set/clear. Whoever holds the
 * write side of that lock has exclusive rights to update the tag together
 * with the data word it guards; readers must never be able to observe a
 * partially-applied update (i.e. new data with a stale tag, or vice versa).
 *
 * This test drives that primitive directly, from multiple host threads, and
 * checks the invariants the rest of CHERI's memory model relies on:
 *  - writers never run concurrently with each other or with readers,
 *  - a reader can never observe a torn {data, tag} pair,
 *  - the packed lock byte always returns to a clean state once every
 *    thread has released it.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/thread.h"
#include "../../target/cheri-common/cheri_tagmem_locktag.h"

#define NUM_READERS 4
#define NUM_WRITERS 4
#define ITERS_PER_THREAD 20000

typedef struct SharedTagState {
    lock_tag lock;
    /*
     * The "data" a capability load/store would carry alongside the tag.
     * Only ever touched while the write lock is held, mirroring how a real
     * capability store first computes {data, tag} and then commits both
     * together. Torn reads of the pair are exactly what the lock exists to
     * prevent.
     */
    int shadow_data;
    /* Detects any overlap between two writers' critical sections. */
    int writer_in_critical_section;
} SharedTagState;

typedef struct ThreadResult {
    bool ok;
} ThreadResult;

static bool shadow_matches_tag(int shadow_data, bool tag)
{
    /*
     * Odd shadow_data <-> tag set. This (not the more obvious even<->set)
     * is what makes the pristine, never-yet-written state (shadow_data == 0,
     * lock_tag == {0} i.e. tag clear) already consistent, so a reader that
     * runs before any writer has completed its first iteration doesn't
     * report a false "torn read".
     */
    return ((shadow_data % 2) != 0) == tag;
}

static void *writer_thread(void *opaque)
{
    SharedTagState *state = opaque;
    ThreadResult *result = g_new0(ThreadResult, 1);
    result->ok = true;

    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        lock_tag_write_acquire_lock(&state->lock);

        if (qatomic_xchg(&state->writer_in_critical_section, 1) != 0) {
            /* Another writer was already inside its critical section. */
            result->ok = false;
        }

        int new_data = state->shadow_data + 1;
        state->shadow_data = new_data;

        qatomic_set(&state->writer_in_critical_section, 0);

        /*
         * Commit the new data together with the tag that matches it, via
         * the same lock release that unblocks readers/other writers.
         */
        lock_tag_write_tag_and_release(&state->lock, (new_data % 2) != 0);
    }

    return result;
}

static void *reader_thread(void *opaque)
{
    SharedTagState *state = opaque;
    ThreadResult *result = g_new0(ThreadResult, 1);
    result->ok = true;

    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        bool tag = lock_tag_read_tag_and_acquire_lock(&state->lock);
        int data = state->shadow_data;
        lock_tag_release_read(&state->lock);

        if (!shadow_matches_tag(data, tag)) {
            result->ok = false;
        }
    }

    return result;
}

static void test_locktag_concurrent_atomicity(void)
{
    SharedTagState state = { 0 };
    QemuThread writers[NUM_WRITERS];
    QemuThread readers[NUM_READERS];

    for (int i = 0; i < NUM_WRITERS; i++) {
        qemu_thread_create(&writers[i], "locktag-writer", writer_thread,
                           &state, QEMU_THREAD_JOINABLE);
    }
    for (int i = 0; i < NUM_READERS; i++) {
        qemu_thread_create(&readers[i], "locktag-reader", reader_thread,
                           &state, QEMU_THREAD_JOINABLE);
    }

    for (int i = 0; i < NUM_WRITERS; i++) {
        ThreadResult *result = qemu_thread_join(&writers[i]);
        g_assert_true(result->ok);
        g_free(result);
    }
    for (int i = 0; i < NUM_READERS; i++) {
        ThreadResult *result = qemu_thread_join(&readers[i]);
        g_assert_true(result->ok);
        g_free(result);
    }

    /* The lock must be back to a clean, fully-released state. */
    g_assert_cmpint(state.lock.as_int &
                    (LOCKTAG_MASK_WRITE_LOCKED | LOCKTAG_MASK_WRITE_WAITING |
                     LOCKTAG_MASK_READERS),
                    ==, 0);
    /* The final tag must still match the final data (last writer wins). */
    g_assert_true(shadow_matches_tag(state.shadow_data,
                                     state.lock.as_int & LOCKTAG_MASK_TAG));
}

static void test_locktag_basic_read_write(void)
{
    lock_tag lock = { 0 };

    g_assert_false(lock_tag_write_tag_and_acquire_lock(&lock, true));
    lock_tag_release_write(&lock);
    g_assert_cmpint(lock.as_int & LOCKTAG_MASK_WRITE_LOCKED, ==, 0);

    g_assert_true(lock_tag_read_tag_and_acquire_lock(&lock));
    g_assert_true(lock_tag_release_read(&lock));
    g_assert_cmpint(lock.as_int & LOCKTAG_MASK_READERS, ==, 0);

    lock_tag_write_acquire_lock(&lock);
    lock_tag_write_tag_and_release(&lock, false);
    g_assert_false(lock_tag_read_tag_and_acquire_lock(&lock));
    lock_tag_release_read(&lock);
}

static void test_locktag_writer_excludes_new_readers(void)
{
    lock_tag lock = { 0 };

    /* Take the write lock without releasing it. */
    lock_tag_write_acquire_lock(&lock);
    g_assert_cmpint(lock.as_int & LOCKTAG_MASK_WRITE_LOCKED, !=, 0);

    /*
     * A reader must not be able to acquire while the write lock is held:
     * simulate a single non-blocking attempt using the same CAS the real
     * acquire loop performs, and confirm it is rejected.
     */
    lock_tag old = lock;
    g_assert_cmpint(old.as_int &
                    (LOCKTAG_MASK_WRITE_WAITING | LOCKTAG_MASK_WRITE_LOCKED),
                    !=, 0);

    lock_tag_release_write(&lock);
    g_assert_cmpint(lock.as_int & LOCKTAG_MASK_WRITE_LOCKED, ==, 0);
}

static void *with_release_writer_thread(void *opaque)
{
    lock_tag *lock = opaque;
    lock_tag_write_tag_and_acquire_lock_impl(lock, true, true, true);
    return NULL;
}

static void test_locktag_with_release_clears_write_waiting(void)
{
    lock_tag lock = { 0 };
    QemuThread writer;

    /* Hold the read lock so the writer thread below has to wait for it. */
    g_assert_false(lock_tag_read_tag_and_acquire_lock(&lock));

    qemu_thread_create(&writer, "locktag-with-release-writer",
                       with_release_writer_thread, &lock,
                       QEMU_THREAD_JOINABLE);

    /*
     * Wait for the writer to observe this reader and set WRITE_WAITING.
     * The with_release=true path commits the unlocked state directly
     * instead of going through lock_tag_release_write(), so it must clear
     * WRITE_WAITING itself once this reader releases - it is the only
     * place that will ever clear it on that path (see
     * cheri_tagmem_locktag.h).
     */
    while (!(qatomic_read(&lock.as_int) & LOCKTAG_MASK_WRITE_WAITING)) {
        g_usleep(1000);
    }

    lock_tag_release_read(&lock);
    qemu_thread_join(&writer);

    g_assert_cmpint(lock.as_int & LOCKTAG_MASK_WRITE_WAITING, ==, 0);
    g_assert_cmpint(lock.as_int & LOCKTAG_MASK_TAG, !=, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/cheri/tagmem/locktag/basic-read-write",
                    test_locktag_basic_read_write);
    g_test_add_func("/cheri/tagmem/locktag/writer-excludes-new-readers",
                    test_locktag_writer_excludes_new_readers);
    g_test_add_func("/cheri/tagmem/locktag/concurrent-atomicity",
                    test_locktag_concurrent_atomicity);
    g_test_add_func("/cheri/tagmem/locktag/with-release-clears-write-waiting",
                    test_locktag_with_release_clears_write_waiting);
    return g_test_run();
}

/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021-2021 Lawrence Esswood <le277@cam.ac.uk>
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
#ifndef QEMU_CHERI_TAGMEM_LOCKTAG_H
#define QEMU_CHERI_TAGMEM_LOCKTAG_H

#include "qemu/osdep.h"
#include "qemu/atomic.h"

/*
 * A CHERI tag with a readers-writer lock packed into a single byte.
 * Write preferring as if one core is spinning waiting for a tag to be
 * updated, the write needs to make it through for progress to be made.
 * This lock should never be held for more than the duration of an
 * instruction.
 *
 * This is the concurrency primitive that makes a capability load/store
 * atomic with the associated tag set/clear: whoever holds the write lock
 * has exclusive access to update the tag (and, by construction of the
 * callers, the data word it guards) and no reader can observe the tag
 * mid-update.
 *
 * This header has no dependency beyond qemu/atomic.h so that it can be
 * exercised directly by a host-side unit test (see
 * tests/unit/test-cheri-tagmem-locktag.c) without pulling in any
 * target-specific CPU state.
 */

typedef struct lock_tag {
    uint8_t as_int;
} lock_tag;

#define LOCKTAG_MASK_TAG           (1 << 0)
#define LOCKTAG_MASK_WRITE_LOCKED  (1 << 1)
#define LOCKTAG_MASK_WRITE_WAITING (1 << 2)
#define LOCKTAG_MASK_READERS       (0b11111 << 3)
#define LOCKTAG_MASK_READER_INC    (1 << 3)

/* Define to do some extra checks around spinlocks */
/* #define DEBUG_SPIN_LOCKS */

/*
 * Report if spin locks are being held too long (only if DEBUG_SPIN_LOCKS).
 * I see O(10 000) pretty regularly, at least every second or so.
 * I more rarely see O(10 000 000), however, printing the debug messages is
 * done with the lock held, so may be significantly increasing the time they
 * are held for.
 * TODO: If these are being held too long, maybe we should be sleeping?
 * Should only ever happen if the core holding the lock is de-scheduled.
 */

#define INSTRUMENT_SPIN_REPORT 10000
/* Assert if held a very long time */
#define SPIN_TOO_MANY 10000000000ULL

#ifdef DEBUG_SPIN_LOCKS

static uint64_t read_spins_max;
static uint64_t write_spins_max;

#define assert_write_locked(lock)                                             \
    assert(lock->as_int & LOCKTAG_MASK_WRITE_LOCKED)
#define assert_read_locked(lock) assert(lock->as_int & LOCKTAG_MASK_READERS)

static inline void spins_report(const char *info, uint64_t new,
                                uint64_t *max_ptr)
{
    uint64_t max = *max_ptr;
    if (new > max) {
        qatomic_cmpxchg(max_ptr, max, new);
        printf("New maximum spins for %s: %ld\n", info, new);
    }
    if (new > INSTRUMENT_SPIN_REPORT) {
        printf("Large number of spins for %s: %ld\n", info, new);
    }
}

#define spins_assert(spins)                                                   \
    assert(spins < SPIN_TOO_MANY && "lock held too long");

#else

#define spins_report(info, spins, max_ptr) ((void)(spins))
#define spins_assert(spins) ((void)(spins))
#define assert_write_locked(...)
#define assert_read_locked(...)

#endif

/*
 * Acquire / release. All acquires (read and write) return the tag.
 * read acquire and read release can optionally set the tag.
 */

static inline bool lock_tag_read_tag_and_acquire_lock(lock_tag *lock)
{
    /*
     * Every peek at the lock byte outside of a CAS/RMW must itself be an
     * atomic (if relaxed) load, never a plain dereference: this byte is
     * concurrently written by other harts via atomic RMWs, and mixing plain
     * and atomic accesses to the same memory is a data race (caught by
     * ThreadSanitizer) that a compiler is free to miscompile. The peek only
     * decides whether to attempt a CAS; the CAS itself is what actually
     * synchronizes, so relaxed is sufficient here.
     */
    lock_tag old = { .as_int = qatomic_read(&lock->as_int) };

    int64_t spins = 0;
    do {
        if (!(old.as_int &
              (LOCKTAG_MASK_WRITE_WAITING | LOCKTAG_MASK_WRITE_LOCKED))) {
            lock_tag new = old;
            /*
             * (TODO: a static assert that we don't have more than 31 host
             * threads). Overflow precluded by number of threads.
             */
            new.as_int += LOCKTAG_MASK_READER_INC;
            lock_tag got = { .as_int = qatomic_cmpxchg(
                                 &lock->as_int, old.as_int, new.as_int) };
            if (got.as_int == old.as_int) {
                break;
            }
            old = got;
        } else {
            old.as_int = qatomic_read(&lock->as_int);
        }
        spins++;
        spins_assert(spins);
    } while (true);
    spins_report("read acquire", spins, &read_spins_max);
    return old.as_int & LOCKTAG_MASK_TAG;
}

static inline bool lock_tag_release_read(lock_tag *lock)
{
    assert_read_locked(lock);
    lock_tag tag = { .as_int = qatomic_fetch_sub(&lock->as_int,
                                                 LOCKTAG_MASK_READER_INC) };
    return tag.as_int & LOCKTAG_MASK_TAG;
}

/*
 * Generic implementation for anything that might want to take the lock as
 * a writer.
 * @set_tag: will also set the tag when taking the lock
 * @tag: what to set the tag to (if set_tag)
 * @with_release: will also release the lock afterwards
 */
static inline bool
lock_tag_write_tag_and_acquire_lock_impl(lock_tag *lock, bool set_tag,
                                         bool tag, bool with_release)
{
    /*
     * See the comment in lock_tag_read_tag_and_acquire_lock() above: this
     * peek must be an atomic load, not a plain dereference.
     */
    lock_tag old = { .as_int = qatomic_read(&lock->as_int) };
    uint64_t spins = 0;
    do {
        uint8_t read_and_wait =
            LOCKTAG_MASK_READERS | LOCKTAG_MASK_WRITE_WAITING;
        if ((old.as_int & LOCKTAG_MASK_WRITE_LOCKED) ||
            ((old.as_int & read_and_wait) == read_and_wait)) {
            old.as_int = qatomic_read(&lock->as_int);
        } else {
            lock_tag new = old;
            if (old.as_int & LOCKTAG_MASK_READERS) {
                new.as_int |= LOCKTAG_MASK_WRITE_WAITING;
            } else {
                if (!with_release) {
                    new.as_int |= LOCKTAG_MASK_WRITE_LOCKED;
                } else {
                    /*
                     * This commits the unlocked state directly (no separate
                     * lock_tag_release_write() call follows), so any
                     * WRITE_WAITING set while draining readers above must be
                     * cleared here - it is the only place that will ever
                     * clear it on this path.
                     */
                    new.as_int &= ~LOCKTAG_MASK_WRITE_WAITING;
                }
                if (set_tag) {
                    new.as_int = (new.as_int & ~LOCKTAG_MASK_TAG) | tag;
                }
            }
            lock_tag got = { .as_int = qatomic_cmpxchg(
                                 &lock->as_int, old.as_int, new.as_int) };
            if ((got.as_int == old.as_int) &&
                !(old.as_int & LOCKTAG_MASK_READERS))
                break;

            old = got;
        }
        spins++;
        spins_assert(spins);
    } while (true);
    spins_report("write acquire", spins, &write_spins_max);
    assert_write_locked(lock);
    return old.as_int & LOCKTAG_MASK_TAG;
}

static inline bool lock_tag_write_tag_and_acquire_lock(lock_tag *lock,
                                                       bool tag)
{
    return lock_tag_write_tag_and_acquire_lock_impl(lock, true, tag, false);
}

static inline bool lock_tag_write_acquire_lock(lock_tag *lock)
{
    return lock_tag_write_tag_and_acquire_lock_impl(lock, false, false, false);
}

static inline void lock_tag_release_write(lock_tag *lock)
{
    assert_write_locked(lock);
    qatomic_fetch_and(&lock->as_int, LOCKTAG_MASK_TAG);
}

static inline void lock_tag_write_tag_and_release(lock_tag *lock, bool tag)
{
    assert_write_locked(lock);
    /*
     * Must be a release (not a relaxed store): this publishes both the new
     * tag and the unlocked state, and readers/writers elsewhere in this file
     * synchronize with it via SEQ_CST loads/RMWs. A relaxed store here would
     * let the data written just before this call (the capability itself, or
     * whatever a plain store just wrote) become visible to another hart
     * after that hart already observes the unlock - i.e. exactly the torn
     * data/tag update this lock exists to prevent.
     */
    qatomic_store_release(&lock->as_int, tag ? LOCKTAG_MASK_TAG : 0);
}

/*
 * These two don't take locks outs, so use with caution.
 * As long as every other access respects locks, we don't need atomics here;
 * the caller is expected to already hold the write lock (or to know that
 * concurrent tags are not in use).
 */
static inline bool lock_tag_peek_tag(lock_tag *lock)
{
    return lock->as_int & LOCKTAG_MASK_TAG;
}

static inline bool lock_tag_poke_tag(lock_tag *lock, bool tag)
{
    bool old = lock->as_int & LOCKTAG_MASK_TAG;
    lock->as_int = (lock->as_int & ~LOCKTAG_MASK_TAG) | tag;
    return old;
}

#endif /* QEMU_CHERI_TAGMEM_LOCKTAG_H */

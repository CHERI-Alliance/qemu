/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) Codasip 2026
 *
 * This work was supported by Innovate UK project 105694, "Digital Security
 * by Design (DSbD) Technology Platform Prototype".
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

#ifndef CHERI_TAG_TXN_TYPES_H
#define CHERI_TAG_TXN_TYPES_H

/*
 * Cheri Tag Transaction API
 * In order to manage the interactions that are required for safe access to a
 * shared tag store we define a transaction API.
 * Operations read/update tags in a single transaction. The operation first
 * probes the required tags to complete the operation. It then acquires all the
 * required locks. The operation can then perform whatever modification is
 * required to both the tags and the data atomically. Finally the locks can be
 * released.
 */

/*
 * A CPU access touches at most two capabilities which is at most two tag blocks
 * and spans.
 */
#define CHERI_TAG_TXN_MAX 2
/*
 * CPU transactions may nest locks e.g. Atomics may need to hold a lock for
 * both a read and write, which may also span tag block.
 */
#define CHERI_TAG_TXN_MAX_HELD (2 * CHERI_TAG_TXN_MAX)

typedef enum {
    CHERI_TAG_TXN_VIRT = 0x01,   /* Virtual access by CPU */
    CHERI_TAG_TXN_FAILED = 0x02, /* Non faulting resolve failed */
    CHERI_TAG_TXN_LOCKED = 0x04,
    /*
     * CPU's open transaction already holds all real blocks required, so this
     * transactions lock and commit do nothing.
     */
    CHERI_TAG_TXN_JOINED = 0x08,
    CHERI_TAG_TXN_LOG = 0x10, /* Log tag changes */
} CheriTagTxnState;

typedef struct CheriTagTxnSpan {
    struct CheriTagBlock_s *block; /* The tag block */
    uint16_t first;                /* Index in the block of first capability */
    uint16_t end; /* Index of last capability + 1 so the span = [first, end).*/
} CheriTagTxnSpan;

/* Cheri Tag Transaction */
typedef struct CheriTagTxn {
    struct CPUState *cpu; /* CPU doing the txn. Null for device writes */
    /*
     * Blocks to lock in order. Real tag blocks by address, then dummy locks
     * by address.
     */
    struct CheriTagBlock_s *lock[CHERI_TAG_TXN_MAX];
    /* Each held blocks seqlock value while locked */
    unsigned long lock_seq[CHERI_TAG_TXN_MAX];
    /* Capabilities whose tags may change */
    CheriTagTxnSpan span[CHERI_TAG_TXN_MAX];
    void *host;    /* Host address for first byte, null for IO/failed resolve */
    uint64_t addr; /* GVA or RAM offset for a physical transaction */
    struct CheriTagTxn *outer; /* Parent transaction */
    uint8_t nlocks;            /* Used entries in lock */
    uint8_t nreal;             /* Real locks in nlocks */
    uint8_t nspans;            /* Used entries in spans */
    uint8_t flags;             /* CheriTagTxnState flags */
    uint8_t held_base; /* The CPU's nheld when transaction was registered */
} CheriTagTxn;

/* Cheri Tag Transaction CPU state (one per cpu) */
typedef struct CheriTagTxnCpu {
    CheriTagTxn *active; /* The innermost registered transaction or NULL */
    /* All locks this cpu holds */
    struct CheriTagBlock_s *held[CHERI_TAG_TXN_MAX_HELD];
    uint8_t nheld;     /* Used entries in held. */
    /*
     * store_open and store record the transaction for a data store
     * that runs between a begin and an invalidate/end call. This includes the
     * TCG store helpers and the MIPS SWL/SWR/SDL/SDR handlers.
     */
    bool store_open;
    CheriTagTxn store;
} CheriTagTxnCpu;
#endif /* CHERI_TAG_TXN_TYPES_H */

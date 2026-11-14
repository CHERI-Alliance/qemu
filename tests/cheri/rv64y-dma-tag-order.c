/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A device write to RAM must clear the tags of the granules it writes
 * atomically with the data: a capability load on another hart may return the
 * old capability or the new bytes untagged, never the new bytes with the old
 * tag. Hart 1 starts a fw_cfg DMA of the 16-byte UUID over a tagged
 * capability and rv64y-dma-tag-order.gdb holds it once the data is in RAM. It
 * then sets GDB_GATE and runs only hart 0, which loads the granule and writes
 * CSR_HOOK.
 *
 * Note: This test was AI-generated for the tests/cheri regression suite.
 * Exit status: 0 pass, 1 new data loaded with the old tag, 2 DMA data not
 * visible, 3 tag still set after the DMA completed.
 */
#include "rvy.h"

/* DMA_TARGET and GDB_GATE are also referenced by rv64y-dma-tag-order.gdb. */
#define DMA_TARGET     0x80010000UL
#define GDB_GATE       0x80010020UL
#define FW_CFG_DMA_REG 0x10100010UL
#define CLINT_MTIMECMP 0x02004000UL
#define FW_CFG_CTL     0x0002000aU /* select UUID (2), read */
#define UUID_LO        0x8877665544332211ULL

struct fw_cfg_dma_access {
    uint32_t control_be;
    uint32_t length_be;
    uint64_t address_be;
} ALIGNED(16);

static struct fw_cfg_dma_access dma_desc;
static uint64_t dma_reg_val_be;
static uint32_t start_flag;
static uint32_t done_flag;

void secondary_main(unsigned long hartid)
{
    if (hartid != 1) {
        return;
    }
    while (!atomic_load_acquire(&start_flag)) {
    }
    *(volatile uint64_t *)FW_CFG_DMA_REG = dma_reg_val_be;
    atomic_store_release(&done_flag, 1);
}

int test_main(void)
{
    volatile uint64_t *mtimecmp = (volatile uint64_t *)CLINT_MTIMECMP;
    volatile uint64_t *dma_target = (volatile uint64_t *)DMA_TARGET;
    volatile uint32_t *gdb_gate = (volatile uint32_t *)GDB_GATE;

    /*
     * Keep mip.MTIP clear so hart 0 never tries to acquire the BQL while
     * hart 1 is stopped inside the DMA write by gdb.
     */
    mtimecmp[0] = ~0ULL;
    mtimecmp[1] = ~0ULL;

    rvy_store_ddc_addr((void *)DMA_TARGET, DMA_TARGET);

    dma_desc.control_be = __builtin_bswap32(FW_CFG_CTL);
    dma_desc.length_be = __builtin_bswap32(16);
    dma_desc.address_be = __builtin_bswap64(DMA_TARGET);
    dma_reg_val_be = __builtin_bswap64((uintptr_t)&dma_desc);

    atomic_store_release(&start_flag, 1);

    while (*gdb_gate == 0) {
    }
    while (*dma_target != UUID_LO) {
    }

    uintptr_t cap_addr = 0;
    bool tagged = rvy_load_cap_tag((const void *)DMA_TARGET, &cap_addr);
    uint64_t observed_data = *dma_target;
    asm volatile("csrw 0x320, x0" ::: "memory");

    check(!tagged || cap_addr == DMA_TARGET, 1);
    check(observed_data == UUID_LO, 2);

    while (!atomic_load_acquire(&done_flag)) {
    }
    check(!rvy_load_cap_tag((const void *)DMA_TARGET, NULL), 3);
    return 0;
}

/*
 * QEMU RISC-V VirtIO machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_VIRT_H
#define HW_RISCV_VIRT_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "hw/block/flash.h"
#include "qom/object.h"

#define VIRT_CPUS_MAX_BITS             9
#define VIRT_CPUS_MAX                  (1 << VIRT_CPUS_MAX_BITS)
#define VIRT_SOCKETS_MAX_BITS          2
#define VIRT_SOCKETS_MAX               (1 << VIRT_SOCKETS_MAX_BITS)

#define TYPE_RISCV_VIRT_MACHINE MACHINE_TYPE_NAME("virt")
typedef struct RISCVVirtState RISCVVirtState;
DECLARE_INSTANCE_CHECKER(RISCVVirtState, RISCV_VIRT_MACHINE,
                         TYPE_RISCV_VIRT_MACHINE)

typedef enum RISCVVirtAIAType {
    VIRT_AIA_TYPE_NONE = 0,
    VIRT_AIA_TYPE_APLIC,
    VIRT_AIA_TYPE_APLIC_IMSIC,
} RISCVVirtAIAType;

struct RISCVVirtState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    Notifier machine_done;
    DeviceState *platform_bus_dev;
    RISCVHartArrayState soc[VIRT_SOCKETS_MAX];
    DeviceState *irqchip[VIRT_SOCKETS_MAX];
    PFlashCFI01 *flash[2];
    FWCfgState *fw_cfg;

    int fdt_size;
    bool have_aclint;
    bool have_clic;
    bool clic_prv_s;
    bool clic_prv_u;
    uint8_t clic_intctlbits;
    char *clic_version;
    RISCVVirtAIAType aia_type;
    int aia_guests;
};

enum {
    VIRT_DEBUG,
    VIRT_MROM,
    VIRT_TEST,
    VIRT_RTC,
    VIRT_CLINT,
    VIRT_ACLINT_SSWI,
    VIRT_CLIC,
    VIRT_PLIC,
    VIRT_APLIC_M,
    VIRT_APLIC_S,
    VIRT_UART0,
    VIRT_VIRTIO,
    VIRT_FW_CFG,
    VIRT_IMSIC_M,
    VIRT_IMSIC_S,
    VIRT_FLASH,
    VIRT_DRAM,
    VIRT_PCIE_MMIO,
    VIRT_PCIE_PIO,
    VIRT_PLATFORM_BUS,
    VIRT_PCIE_ECAM
};

enum {
    UART0_IRQ = 10,
    RTC_IRQ = 11,
    VIRTIO_IRQ = 1, /* 1 to 8 */
    VIRTIO_COUNT = 8,
    PCIE_IRQ = 0x20, /* 32 to 35 */
    VIRT_PLATFORM_BUS_IRQ = 64, /* 64 to 96 */
    VIRTIO_NDEV = 96 /* Arbitrary maximum number of interrupts */
};

#define VIRT_PLATFORM_BUS_NUM_IRQS 32

#define VIRT_IRQCHIP_IPI_MSI 1
#define VIRT_IRQCHIP_NUM_MSIS 255
#define VIRT_IRQCHIP_NUM_SOURCES VIRTIO_NDEV
#define VIRT_IRQCHIP_NUM_PRIO_BITS 3
#define VIRT_IRQCHIP_MAX_GUESTS_BITS 3
#define VIRT_IRQCHIP_MAX_GUESTS ((1U << VIRT_IRQCHIP_MAX_GUESTS_BITS) - 1U)

#define VIRT_PLIC_PRIORITY_BASE 0x00
#define VIRT_PLIC_PENDING_BASE 0x1000
#define VIRT_PLIC_ENABLE_BASE 0x2000
#define VIRT_PLIC_ENABLE_STRIDE 0x80
#define VIRT_PLIC_CONTEXT_BASE 0x200000
#define VIRT_PLIC_CONTEXT_STRIDE 0x1000
#define VIRT_PLIC_SIZE(__num_context) \
    (VIRT_PLIC_CONTEXT_BASE + (__num_context) * VIRT_PLIC_CONTEXT_STRIDE)

#define VIRT_CLIC_INTCLTBITS    3
#define VIRT_CLIC_VERSION       "v0.9"
#define VIRT_CLIC_MAX_IRQS      0x1000
#define VIRT_CLIC_CONTEXT_BASE  0x1000
#define VIRT_CLIC_CONTEXT_COUNT(_prv_s, _prv_u) \
    (1 + ((_prv_s) ? 1 : 0) + ((_prv_u) ? 1 : 0))
#define VIRT_CLIC_FULL_CONTEXT_COUNT VIRT_CLIC_CONTEXT_COUNT(1, 1)
#define VIRT_CLIC_ALIGN_BITS    12
#define VIRT_CLIC_ALIGN_MASK    ((1U << VIRT_CLIC_ALIGN_BITS) - 1)
/* Round up to next 4KiB alignment boundary */
#define VIRT_CLIC_ALIGN(_base_addr) \
    (((_base_addr) + VIRT_CLIC_ALIGN_MASK) & VIRT_CLIC_ALIGN_MASK)
#define VIRT_CLIC_INT_SIZE(_irq_count) ((_irq_count) * 4)
/*
 * The spec doesn't define a memory layout, other than to say that each
 * CLIC should be on a 4KiB boundary if memory-mapped.
 * This implementation makes all the CLICs contiguous, in the order M, S, U,
 * and assumes the worst-case size.
 */
#define VIRT_CLIC_BLOCK_SIZE \
    (VIRT_CLIC_CONTEXT_BASE + VIRT_CLIC_INT_SIZE(VIRT_CLIC_MAX_IRQS))
#define VIRT_CLIC_HART_SIZE(_prv_s, _prv_u) \
    (VIRT_CLIC_CONTEXT_COUNT(_prv_s, _prv_u) * VIRT_CLIC_BLOCK_SIZE)
#define VIRT_CLIC_SIZE(_hart_count, _prv_s, _prv_u) \
    ((_hart_count) * VIRT_CLIC_HART_SIZE(_prv_s, _prv_u))
#define VIRT_CLIC_MAX_HART_SIZE VIRT_CLIC_HART_SIZE(1, 1)
#define VIRT_CLIC_MAX_SIZE(_hart_count) \
    ((_hart_count) * VIRT_CLIC_MAX_HART_SIZE)

#define FDT_PCI_ADDR_CELLS    3
#define FDT_PCI_INT_CELLS     1
#define FDT_PLIC_INT_CELLS    1
#define FDT_APLIC_INT_CELLS   2
#define FDT_IMSIC_INT_CELLS   0
#define FDT_MAX_INT_CELLS     2
#define FDT_MAX_INT_MAP_WIDTH (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_MAX_INT_CELLS)
#define FDT_PLIC_INT_MAP_WIDTH  (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_PLIC_INT_CELLS)
#define FDT_APLIC_INT_MAP_WIDTH (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_APLIC_INT_CELLS)

#endif

/*
 * QEMU RISC-V Hobgoblin Board
 *
 * Copyright (c) 2023 Stuart Menefy, stuart.menefy@codasip.com
 * Copyright (c) 2023 Codasip Limited
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

#ifndef HW_RISCV_HOBGOBLIN_H
#define HW_RISCV_HOBGOBLIN_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"

#define TYPE_HOBGOBLIN_MACHINE          MACHINE_TYPE_NAME("hobgoblin")
#define TYPE_HOBGOBLIN_GENESYS2_MACHINE MACHINE_TYPE_NAME("hobgoblin-genesys2")
#define TYPE_HOBGOBLIN_PROFPGA_MACHINE  MACHINE_TYPE_NAME("hobgoblin-profpga")

enum board_type {
    BOARD_TYPE_GENESYS2,
    BOARD_TYPE_PROFPGA,
};

enum eth_type {
    ETH_TYPE_AXI_ETHERNET,
    ETH_TYPE_ETHERNETLITE,
};

struct HobgoblinClass {
    MachineClass parent;
    enum board_type board_type;
};

struct HobgoblinState {
    /*< private >*/
    MachineState machine; /* QOM: derived from MachineState/TYPE_MACHINE */
    /*< properties >*/
    bool boot_from_rom;
    enum eth_type eth_type;
    /*< devices >*/
    RISCVHartArrayState soc;
    DeviceState *plic;
    DeviceState *spi;
    DeviceState *gpio[2];
    DeviceState *eth;
    DeviceState *trng;
    DeviceState *timer;
};

OBJECT_DECLARE_TYPE(HobgoblinState, HobgoblinClass, HOBGOBLIN_MACHINE)

enum {
    HOBGOBLIN_MROM,
    HOBGOBLIN_BOOT_ROM,
    HOBGOBLIN_BOOT_RAM,
    HOBGOBLIN_SRAM, /* exists on FPGA only */
    HOBGOBLIN_PLIC,
    HOBGOBLIN_ID_REG,
    HOBGOBLIN_CLINT,
    HOBGOBLIN_ETHLITE,
    HOBGOBLIN_AXI_ETH,
    HOBGOBLIN_AXI_DMA,
    HOBGOBLIN_UART0,
    HOBGOBLIN_SPI,
    HOBGOBLIN_GPIO0,
    HOBGOBLIN_GPIO1,
    HOBGOBLIN_TRNG,
    HOBGOBLIN_TIMER,
    HOBGOBLIN_VIRTIO,
    HOBGOBLIN_DRAM
};

#define NUM_VIRTIO_TRANSPORTS   4

enum {
    /* Interrupt 0 is reserved */
    HOBGOBLIN_UART0_IRQ    = 1,
    HOBGOBLIN_SPI_IRQ      = 4,
    HOBGOBLIN_GPIO0_IRQ    = 7,
    HOBGOBLIN_GPIO1_IRQ    = 8,
    HOBGOBLIN_TIMER_IRQ    = 9,
    HOBGOBLIN_ETH_IRQ      = 15,
    HOBGOBLIN_AXIDMA_IRQ0  = 16,
    HOBGOBLIN_AXIDMA_IRQ1  = 17,
    HOBGOBLIN_VIRTIO0_IRQ  = 28,
    HOBGOBLIN_VIRTIO3_IRQ  = 31,
    /* ----------- */
    HOBGOBLIN_MAX_IRQ
};

#endif /* HW_RISCV_HOBGOBLIN_H */

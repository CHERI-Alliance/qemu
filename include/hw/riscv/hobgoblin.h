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

#define MACHINE_TYPE_HOBGOBLIN  MACHINE_TYPE_NAME("hobgoblin")

typedef struct {
    /*< private >*/
    MachineState machine; /* QOM: derived from MachineState/TYPE_MACHINE */
    /*< properties >*/
    bool boot_from_rom;
    /*< devices >*/
    RISCVHartArrayState soc;
    DeviceState *plic;
    DeviceState *spi;
    DeviceState *gpio[2];
    DeviceState *eth;
} HobgoblinState_t;

#define HOBGOBLIN_MACHINE_STATE(obj) \
    OBJECT_CHECK(HobgoblinState_t, (obj), MACHINE_TYPE_HOBGOBLIN);

enum {
    HOBGOBLIN_MROM,
    HOBGOBLIN_BOOT_ROM,
    HOBGOBLIN_BOOT_RAM,
    HOBGOBLIN_SRAM, /* exists on FPGA only */
    HOBGOBLIN_PLIC,
    HOBGOBLIN_CLINT,
    HOBGOBLIN_ETH,
    HOBGOBLIN_UART0,
    HOBGOBLIN_SPI,
    HOBGOBLIN_GPIO0,
    HOBGOBLIN_GPIO1,
    HOBGOBLIN_DRAM
};

enum {
    /* Interrupt 0 is reserved */
    HOBGOBLIN_UART0_IRQ    = 1,
    HOBGOBLIN_SPI_IRQ      = 4,
    HOBGOBLIN_GPIO0_IRQ    = 7,
    HOBGOBLIN_GPIO1_IRQ    = 8,
    HOBGOBLIN_ETH_IRQ      = 15,
    /* ----------- */
    HOBGOBLIN_MAX_IRQ
};

#endif /* HW_RISCV_HOBGOBLIN_H */

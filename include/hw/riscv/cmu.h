/*
 * QEMU RISC-V CMU Device
 *
 * Copyright (c) 2024 Codasip Limited s.r.o
 *
 * This implements a dummy CMU device only capable of invalidating a region
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

#ifndef HW_CMU_H
#define HW_CMU_H

#include "qom/object.h"
#include "hw/sysbus.h"

// Version Register Fields for V2 Regmap
#define CMU_VENDOR_ID 0x503l // Codasip
#define CMU_CBASE_ID 0xfal // Codasip Codebase ID for CMU
#define CMU_VENDOR_N_CBASE_ID_DEFAULT (CMU_VENDOR_ID | (CMU_CBASE_ID << 32))
#define CMU_CONFIG_ID 0x1l
#define CMU_VERSION_ID 0x1l
#define CMU_CONFIG_N_VERSION_ID_DEFAULT (CMU_CONFIG_ID | (CMU_VERSION_ID << 32))

// Features Register fields
//V1 Reg Map Version field
#define CMU_MINOR_VN          (0x1l << 0)
#define CMU_MAJOR_VN          (0x0l << 4)
#define CMU_PERIP_ID          (0x50l << 8)

#define CMU_FT_TS             (0x1l << 0)
#define CMU_FT_TC             (0x1l << 1)
#define CMU_FT_TG             (0x1l << 2)
#define CMU_FT_HPM            (0x1l << 3)
#define CMU_FT_RAS            (0x1l << 4)
#define CMU_FT_CLEN128        (0x1l << 5)
#define CMU_FT_AFG            (0x10l << 6)  // 16
#define CMU_FT_LOG2AFTSZ      (0x3l << 12)  // log2(8)
#define CMU_FT_LOG2LMW_BYTE   (0x2l << 15)  // log2(4)
#define CMU_FT_LOG2LMSZ_KBYTE (0x8l << 18)  // log2(256)
#define CMU_FT_PASZ_BYTE      (0x28l << 26) // 40
#define CMU_V2_FT_CONST_DEFAULT                                                   \
    (CMU_FT_TS | CMU_FT_TC | CMU_FT_TG | CMU_FT_HPM | CMU_FT_RAS |	       \
     CMU_FT_CLEN128 | CMU_FT_AFG | CMU_FT_LOG2AFTSZ | CMU_FT_LOG2LMW_BYTE |    \
     CMU_FT_LOG2LMSZ_KBYTE | CMU_FT_PASZ_BYTE)

#define CMU_V1_FT_CONST_DEFAULT (CMU_MINOR_VN | CMU_MAJOR_VN | CMU_PERIP_ID |  \
 (CMU_V2_FT_CONST_DEFAULT << 16))

#define TYPE_CMU_DEVICE "riscv-cmu"
#define CMU_REGION_SIZE (1024 * 64)
#define CMU_REGS_SIZE (0x2b0) // Only support the registers not memory windows

#define CMU_V1_TISTART 0x28
#define CMU_V1_TIEND   0x30
#define CMU_V1_TCMO    0x20

#define CMU_V2_TISTART 0x38
#define CMU_V2_TIEND   0x40
#define CMU_V2_TCMO    0x30
 
#define REG_CMU_TISTART(v) ((v != 0?CMU_V2_TISTART:CMU_V1_TISTART)/sizeof(uint64_t))
#define REG_CMU_TIEND(v)   ((v != 0?CMU_V2_TIEND:CMU_V1_TIEND)/sizeof(uint64_t))
#define REG_CMU_TCMO(v)   ((v != 0?CMU_V2_TCMO:CMU_V1_TCMO)/sizeof(uint64_t))

#define CMU_TCMO_ACTIVE 0x1
#define CMU_TI_ACTIVE 0x1
#if defined(TARGET_RISCV64)
#define LOG2_CMU_CLEN 7
#elif defined(TARGET_RISCV32)
#define LOG2_CMU_CLEN 6
#else
#error Should only be used on RISCV machines
#endif 
#define CMU_PHYSICAL_ADDRESS_SIZE 56

OBJECT_DECLARE_TYPE(CMUDeviceState, CMUClass, CMU_DEVICE)

typedef struct CMUDeviceState CMUDeviceState;
struct CMUDeviceState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    MemoryRegion *managed;
    hwaddr base;
    hwaddr size;
    uint16_t cache_line_size;
    uint32_t reg_map_ver; // Version of the register map
    uint64_t regs[(CMU_REGS_SIZE) / sizeof(uint64_t)];
};

struct CMUClass {
    SysBusDeviceClass parent_class;

    void (*invalidate_region)(RAMBlock *ram, ram_addr_t ram_offset, ram_addr_t len);
};

#endif

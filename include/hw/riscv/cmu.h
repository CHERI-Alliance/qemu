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

// Features Register fields
#define CMU_MINOR_VN          (0x1l << 0)
#define CMU_MAJOR_VN          (0x0l << 4)
#define CMU_PERIP_ID          (0x50l << 8)
#define CMU_FT_TS             (0x1l << 16)
#define CMU_FT_TC             (0x1l << 17)
#define CMU_FT_TG             (0x1l << 18)
#define CMU_FT_HPM            (0x1l << 19)
#define CMU_FT_RAS            (0x1l << 20)
#define CMU_FT_CLEN128        (0x1l << 21)
#define CMU_FT_AFG            (0x10l << 22) // 16
#define CMU_FT_LOG2AFTSZ      (0x3l << 28)  // log2(8)
#define CMU_FT_LOG2LMW_BYTE   (0x2l << 31)  // log2(4)
#define CMU_FT_LOG2LMSZ_KBYTE (0x8l << 34)  // log2(256)
#define CMU_FT_LOG2TCLSZ_BYTE (0x6l << 38)  // log2(512) - 3
#define CMU_FT_PASZ_BYTE      (0x28l << 42) // 40
#define CMU_FT_DEFAULT                                                         \
    (CMU_MINOR_VN | CMU_MAJOR_VN | CMU_PERIP_ID | CMU_FT_TS | CMU_FT_TC |      \
     CMU_FT_TG | CMU_FT_HPM | CMU_FT_RAS | CMU_FT_CLEN128 | CMU_FT_AFG |       \
     CMU_FT_LOG2AFTSZ | CMU_FT_LOG2LMW_BYTE | CMU_FT_LOG2LMSZ_KBYTE |          \
     CMU_FT_LOG2TCLSZ_BYTE | CMU_FT_PASZ_BYTE)

#define TYPE_CMU_DEVICE "riscv.cmu"
#define CMU_REGION_SIZE (1024 * 64)
#define CMU_REGS_SIZE (0x2b0) // Only support the registers not memory windows

#define CMU_TISTART 0x28
#define CMU_TIEND   0x30
#define CMU_TCMO    0x20

#define REG_CMU_TISTART (CMU_TISTART/sizeof(uint64_t))
#define REG_CMU_TIEND   (CMU_TIEND/sizeof(uint64_t))


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
    uint64_t regs[(CMU_REGS_SIZE) / sizeof(uint64_t)];
};

struct CMUClass {
    SysBusDeviceClass parent_class;

    void (*invalidate_region)(RAMBlock *ram, ram_addr_t ram_offset, ram_addr_t len);
};

#endif

/*
 * QEMU RISC-V CPU QOM header
 *
 * Copyright (c) 2023 Ventana Micro Systems Inc.
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

#ifndef RISCV_CPU_QOM_H
#define RISCV_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_RISCV_CPU "riscv-cpu"
#define TYPE_RISCV_DYNAMIC_CPU "riscv-dynamic-cpu"

#define RISCV_CPU_TYPE_SUFFIX "-" TYPE_RISCV_CPU
#define RISCV_CPU_TYPE_NAME(name) (name RISCV_CPU_TYPE_SUFFIX)
#define CPU_RESOLVING_TYPE TYPE_RISCV_CPU

#define TYPE_RISCV_CPU_ANY              RISCV_CPU_TYPE_NAME("any")
#define TYPE_RISCV_CPU_BASE32           RISCV_CPU_TYPE_NAME("rv32")
#define TYPE_RISCV_CPU_BASE64           RISCV_CPU_TYPE_NAME("rv64")
#define TYPE_RISCV_CPU_BASE128          RISCV_CPU_TYPE_NAME("x-rv128")
#define TYPE_RISCV_CPU_IBEX             RISCV_CPU_TYPE_NAME("lowrisc-ibex")
#define TYPE_RISCV_CPU_SHAKTI_C         RISCV_CPU_TYPE_NAME("shakti-c")
#define TYPE_RISCV_CPU_SIFIVE_E31       RISCV_CPU_TYPE_NAME("sifive-e31")
#define TYPE_RISCV_CPU_SIFIVE_E34       RISCV_CPU_TYPE_NAME("sifive-e34")
#define TYPE_RISCV_CPU_SIFIVE_E51       RISCV_CPU_TYPE_NAME("sifive-e51")
#define TYPE_RISCV_CPU_SIFIVE_U34       RISCV_CPU_TYPE_NAME("sifive-u34")
#define TYPE_RISCV_CPU_SIFIVE_U54       RISCV_CPU_TYPE_NAME("sifive-u54")
#define TYPE_RISCV_CPU_THEAD_C906       RISCV_CPU_TYPE_NAME("thead-c906")
#define TYPE_RISCV_CPU_CODASIP_A730_FLINT                                      \
    RISCV_CPU_TYPE_NAME("codasip-a730-flint")
#define TYPE_RISCV_CPU_CODASIP_L730_GLIM                                       \
    RISCV_CPU_TYPE_NAME("codasip-l730-glim")
#define TYPE_RISCV_CPU_CODASIP_L739_TOPAZ                                      \
    RISCV_CPU_TYPE_NAME("codasip-l739-topaz")
#define TYPE_RISCV_CPU_CODASIP_X730_LUX RISCV_CPU_TYPE_NAME("codasip-x730-lux")
#define TYPE_RISCV_CPU_CODASIP_X730     RISCV_CPU_TYPE_NAME("codasip-x730")
#define TYPE_RISCV_CPU_CODASIP_V730_SHINE                                      \
    RISCV_CPU_TYPE_NAME("codasip-v730-shine")
#define TYPE_RISCV_CPU_CODASIP_V739_SPINEL                                     \
    RISCV_CPU_TYPE_NAME("codasip-v739-spinel")
#define TYPE_RISCV_CPU_CODASIP_V739 RISCV_CPU_TYPE_NAME("codasip-v739")
#define TYPE_RISCV_CPU_CODASIP_H730_GARNET                                     \
    RISCV_CPU_TYPE_NAME("codasip-h730-garnet")
#define TYPE_RISCV_CPU_CODASIP_Y730_QUARTZ                                     \
    RISCV_CPU_TYPE_NAME("codasip-y730-quartz")
#define TYPE_RISCV_CPU_CODASIP_1110_ARIA                                       \
    RISCV_CPU_TYPE_NAME("codasip-1110-aria")
#define TYPE_RISCV_CPU_CODASIP_1110_ASTER                                      \
    RISCV_CPU_TYPE_NAME("codasip-1110-aster")
#define TYPE_RISCV_CPU_CODASIP_1110_APEX                                       \
    RISCV_CPU_TYPE_NAME("codasip-1110-apex")
#define TYPE_RISCV_CPU_VEYRON_V1        RISCV_CPU_TYPE_NAME("veyron-v1")
#define TYPE_RISCV_CPU_HOST             RISCV_CPU_TYPE_NAME("host")

#if defined(TARGET_RISCV32)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE64
#endif

typedef struct CPUArchState CPURISCVState;

OBJECT_DECLARE_CPU_TYPE(RISCVCPU, RISCVCPUClass, RISCV_CPU)

/**
 * RISCVCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A RISCV CPU model.
 */
struct RISCVCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};
#endif /* RISCV_CPU_QOM_H */

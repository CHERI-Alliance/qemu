/*
 * RISC-V CLIC(Core Local Interrupt Controller) interface.
 *
 * Copyright (c) 2021 T-Head Semiconductor Co., Ltd. All rights reserved.
 * Copyright (c) 2024 Cirrus Logic, Inc
 *      and Cirrus Logic International Semiconductor Ltd.
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
 *
 * This implementation follows the CLIC 0.9-stable draft at 14 March 2024,
 * with the following exceptions and implementation details:
 *  - the CLIC control registers are memory-mapped as per earlier drafts (in
 *    particular version 0.9-draft, 20 June 2023)
 *  - the indirect CSR control in 0.9-stable is not implemented
 *  - the vector table can be either handler addresses (as per the spec)
      or a jump table where each entry is processed as an instruction,
      selectable with version number v0.9-jmp
 *  - each hart is assigned its own CLIC block
 *  - if PRV_S and/or PRV_M are supported, they are currently assumed to follow
 *    the PRV_M registers; a subsequent update will address this
 *  - support for PRV_S and PRV_M is selectable at CLIC instantiation
 *  - PRV_S and PRV_U registers are currently separate from PRV_M; a subsequent
 *    update will turn them into filtered views onto the PRV_M registers
 *  - each hart is assigned its own CLIC block
 *  - support for PRV_S and PRV_M is selectable at CLIC instantiation by
 *    passing in a base address for the given modes; a base address of 0 is
 *    treated as not supported
 *  - PRV_S and PRV_U registers are mapped  onto the PRV_M controls with
 *    appropriate filtering for the access mode
 *
 * The implementation has a RISCVCLICState per hart, with a RISCVCLICView
 * for each mode subsidiary to that. Each view knows its access mode and base
 * address, as well as the RISCVCLICState with which it is associated.
 *
 * MMIO accesses go through the view, allowing the appropriate permissions to
 * be enforced when accessing the parent RISCVCLICState for the settings.
 */

#ifndef RISCV_CLIC_H
#define RISCV_CLIC_H

#include "hw/irq.h"
#include "hw/sysbus.h"

#define TYPE_RISCV_CLIC "riscv_clic"
#define TYPE_RISCV_CLIC_VIEW "riscv_clic_view"
#define RISCV_CLIC(obj) \
    OBJECT_CHECK(RISCVCLICState, (obj), TYPE_RISCV_CLIC)
#define RISCV_CLIC_VIEW(obj) \
    OBJECT_CHECK(RISCVCLICView, (obj), TYPE_RISCV_CLIC_VIEW)

/*
 * CLIC per hart active interrupts
 *
 * We maintain per hart lists of enabled interrupts sorted by
 * mode+level+priority. The sorting is done on the configuration path
 * so that the interrupt delivery fastpath can linear scan enabled
 * interrupts in priority order.
 */
typedef struct CLICActiveInterrupt {
    uint16_t intcfg;
    uint16_t irq;
} CLICActiveInterrupt;

typedef enum TRIG_TYPE {
    POSITIVE_LEVEL,
    POSITIVE_EDGE,
    NEG_LEVEL,
    NEG_EDGE,
} TRIG_TYPE;

#define CLIC_INTCTL_BASE        0x1000  /* start offset of intctl registers */
#define MAX_CLIC_INTCTLBITS     8       /* maximum value for intctlbits */

/* maximum of 4096 IRQs */
#define CLIC_IRQ_BITS           12
#define CLIC_MAX_IRQ_COUNT      (1 << CLIC_IRQ_BITS)
#define CLIC_MAX_IRQ            (CLIC_MAX_IRQ_COUNT - 1)
#define CLIC_IRQ_MASK           CLIC_MAX_IRQ

/*
 * clicinttrig registers
 * 31       interrupt_trap_enable
 * 30       nxti_enable
 * 29:13    reserved (WARL 0)
 * 12:0     interrupt_number
 */
#define CLIC_INTTRIG_REGS       32      /* inttrig register count */
#define CLIC_INTTRIG_START      0x10    /* first inttrig register */
#define CLIC_INTTRIG_END        (CLIC_INTTRIG_START + CLIC_INTTRIG_REGS - 1)
#define CLIC_INTTRIG_TRAP_ENA   0x80000000
#define CLIC_INTTRIG_NXTI_ENA   0x40000000
#define CLIC_INTTRIG_IRQN       0x00001fff
#define CLIC_INTTRIG_MASK       (CLIC_INTTRIG_TRAP_ENA | \
                                 CLIC_INTTRIG_NXTI_ENA | CLIC_INTTRIG_IRQN)

/*
 * We combine the mode and intctl to a number so that higher modes come first.
 * 9:8  machine mode
 * 7:0  clicintctl
 */
#define CLIC_INTCFG_MODE_SHIFT  8
#define CLIC_INTCFG_MODE        0x300
#define CLIC_INTCFG_CTL         0xff
#define CLIC_INTCFG_MASK        (CLIC_INTCFG_MODE | CLIC_INTCFG_CTL)

/*
 * clicintattr layout
 * 7:6  mode
 * 5:3  reserved (WPRI 0)
 * 2:1  trig
 * 0    shv
 */
#define CLIC_INTATTR_MODE_SHIFT     6
#define CLIC_INTATTR_MODE_WIDTH     2
#define CLIC_INTATTR_MODE           0xc0
#define CLIC_INTATTR_TRIG_SHIFT     1
#define CLIC_INTATTR_TRIG_WIDTH     2
#define CLIC_INTATTR_TRIG           0x06
#define CLIC_INTATTR_SHV            0x01
#define CLIC_INTATTR_MASK           (CLIC_INTATTR_MODE | CLIC_INTATTR_TRIG | \
                                     CLIC_INTATTR_SHV)
/* The clicintattr value */
#define CLIC_INTATTR_TRIG_EDGE      0b01    /* trig decode edge-triggered */
#define CLIC_INTATTR_TRIG_INV       0b10    /* trig decode negative polarity */

/* Forward declaration */
typedef struct RISCVCLICView RISCVCLICView;

/*
 * The main CLIC state (PRV_M mode) for a hart.
 */
typedef struct RISCVCLICState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    /* Implementation parameters */
    bool shv_enabled;       /* hardware-vectoring enabled */
    bool jump_table;        /* vector with jump table, not handler addresses */
    int hartid;
    uint32_t num_sources;
    uint32_t clic_size;
    uint32_t clic_mmode_base;
    uint32_t clicintctlbits;
    RISCVCLICView *prv_m;    /* our PRV_M view */
    RISCVCLICView *prv_s;    /* our PRV_S view */
    RISCVCLICView *prv_u;    /* our PRV_U view */
    char *version;

    /* Global configuration */
    uint8_t nmbits;         /* mode bits */
    uint8_t mnlbits;        /* level bits for M-mode */
    uint8_t snlbits;        /* level bits for S-mode, if present */
    uint8_t unlbits;        /* level bits for U-mode, if present */
    uint32_t clicinttrig[CLIC_INTTRIG_REGS];

    /* Aperture configuration */
    uint8_t *clicintip;
    uint8_t *clicintie;
    uint8_t *clicintattr;
    uint8_t *clicintctl;

    /* Compatible with v0.8 */
    uint32_t mintthresh;
    uint32_t sintthresh;
    uint32_t uintthresh;

    /* QEMU implementation related fields */
    uint32_t exccode;
    CLICActiveInterrupt *active_list;
    size_t active_count;
    qemu_irq cpu_irq;
} RISCVCLICState;

/*
 * A PRV_S or PRV_U overlay onto the main RISCVCLICState.
 */
typedef struct RISCVCLICView {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVCLICState *clic;       /* the CLIC this is a view onto */
    MemoryRegion mmio;
    uint64_t clicbase;
    uint8_t mode;
} RISCVCLICView;

DeviceState *riscv_clic_create(hwaddr mclicbase, hwaddr sclicbase,
                               hwaddr uclicbase, uint32_t hartid,
                               uint32_t num_sources, uint8_t clicintctlbits,
                               const char *version);

void riscv_clic_decode_exccode(uint32_t exccode, int *mode, int *il, int *irq);
void riscv_clic_clean_pending(void *opaque, int irq);
bool riscv_clic_edge_triggered(void *opaque, int irq);
bool riscv_clic_shv_interrupt(void *opaque, int irq);
bool riscv_clic_use_jump_table(void *opaque);
void riscv_clic_get_next_interrupt(void *opaque);
bool riscv_clic_is_clic_mode(CPURISCVState *env);
#endif

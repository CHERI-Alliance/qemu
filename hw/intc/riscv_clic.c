/*
 * RISC-V CLIC(Core Local Interrupt Controller) for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 * Copyright (c) 2021 T-Head Semiconductor Co., Ltd. All rights reserved.
 * Copyright (c) 2024 Cirrus Logic, Inc and
 *      Cirrus Logic International Semiconductor Ltd.
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "hw/sysbus.h"
#include "sysemu/qtest.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/intc/riscv_clic.h"

static const char *modeview_name[] = {
    TYPE_RISCV_CLIC "_prv_u",       /* PRV_U */
    TYPE_RISCV_CLIC "_prv_s",       /* PRV_S */
    NULL,                           /* reserved */
    TYPE_RISCV_CLIC "_prv_m",       /* PRV_M */
};

/*
 * The 2-bit trig WARL field specifies the trigger type and polarity for each
 * interrupt input. Bit 1, trig[0], is defined as "edge-triggered"
 * (0: level-triggered, 1: edge-triggered); while bit 2, trig[1], is defined as
 * "negative-edge" (0: positive-edge, 1: negative-edge). (Section 3.6)
 */

static inline TRIG_TYPE
riscv_clic_get_trigger_type(RISCVCLICState *clic, size_t irq)
{
    return get_field(clic->clicintattr[irq], CLIC_INTATTR_TRIG);
}

static inline bool
riscv_clic_is_edge_triggered(RISCVCLICState *clic, size_t irq)
{
    TRIG_TYPE trig_type = riscv_clic_get_trigger_type(clic, irq);
    return trig_type & CLIC_INTATTR_TRIG_EDGE;
}

static inline bool
riscv_clic_is_shv_interrupt(RISCVCLICState *clic, size_t irq)
{
    uint32_t shv = get_field(clic->clicintattr[irq], CLIC_INTATTR_SHV);
    return shv && clic->shv_enabled;
}

static uint8_t
riscv_clic_get_interrupt_level(RISCVCLICState *clic, uint8_t intctl)
{
    int nlbits = MIN(clic->mnlbits, clic->clicintctlbits);

    uint8_t mask_il = ((1 << nlbits) - 1) << (8 - nlbits);
    uint8_t mask_padding = (1 << (8 - nlbits)) - 1;
    /* unused level bits are set to 1 */
    return (intctl & mask_il) | mask_padding;
}

static uint8_t
riscv_clic_get_interrupt_priority(RISCVCLICState *clic, uint8_t intctl)
{
    int npbits = clic->clicintctlbits - clic->mnlbits;
    uint8_t mask_priority = ((1 << npbits) - 1) << (8 - npbits);
    uint8_t mask_padding = (1 << (8 - npbits)) - 1;

    if (npbits < 0) {
        return UINT8_MAX;
    }
    /* unused priority bits are set to 1 */
    return (intctl & mask_priority) | mask_padding;
}

static void
riscv_clic_intcfg_decode(RISCVCLICState *clic, uint16_t intcfg,
                         uint8_t *mode, uint8_t *level,
                         uint8_t *priority)
{
    *mode = intcfg >> 8;
    *level = riscv_clic_get_interrupt_level(clic, intcfg & 0xff);
    *priority = riscv_clic_get_interrupt_priority(clic, intcfg & 0xff);
}

static void riscv_clic_next_interrupt(void *opaque)
{
    /*
     * Scan active list for highest priority pending interrupts
     * comparing against this harts mintstatus register and interrupt
     * the core if we have a higher priority interrupt to deliver
     */
    RISCVCLICState *clic = (RISCVCLICState *)opaque;
    CPUState *cpu = cpu_by_arch_id(clic->hartid);
    CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
    bool locked = false;
    if (!env) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "aclint-swi: invalid hartid: %u", clic->hartid);
        return;
    }

    /*
     * Instead of BQL_LOCK_GUARD
     */
    if (!qemu_mutex_iothread_locked()) {
        locked = true;
        qemu_mutex_lock_iothread();
    }

    int il[4] = {
        MAX(get_field(env->mintstatus, MINTSTATUS_UIL),
            clic->uintthresh & 0xff), /* PRV_U */
        MAX(get_field(env->mintstatus, MINTSTATUS_SIL),
            clic->sintthresh & 0xff), /* PRV_S */
        0,                     /* reserved */
        MAX(get_field(env->mintstatus, MINTSTATUS_MIL),
            clic->mintthresh & 0xff)  /* PRV_M */
    };

    /* Get sorted list of enabled interrupts for this hart */
    CLICActiveInterrupt *active = clic->active_list;
    size_t active_count = clic->active_count;
    uint8_t mode, level, priority;

    /* Loop through the enabled interrupts sorted by mode+priority+level */
    while (active_count) {
        riscv_clic_intcfg_decode(clic, active->intcfg, &mode, &level,
                                 &priority);
        if (mode < env->priv || (mode == env->priv && level < il[mode])) {
            /*
             * No pending interrupts with high enough mode+priority+level
             * break and clear pending interrupt for this hart
             */
            break;
        }
        /* Check pending interrupt with high enough mode+priority+level */
        if (clic->clicintip[active->irq]) {
            /* Clean vector edge-triggered pending */
            if (riscv_clic_is_edge_triggered(clic, active->irq) &&
                riscv_clic_is_shv_interrupt(clic, active->irq)) {
                clic->clicintip[active->irq] = 0;
            }
            /* Post pending interrupt for this hart */
            clic->exccode = active->irq |
                            mode << RISCV_EXCP_CLIC_MODE_SHIFT |
                            level << RISCV_EXCP_CLIC_LEVEL_SHIFT;
            qemu_set_irq(clic->cpu_irq, 1);
            if (locked) {
                qemu_mutex_unlock_iothread();
            }
            return;
        }
        /* Check next enabled interrupt */
        active_count--;
        active++;
    }
    if (locked) {
        qemu_mutex_unlock_iothread();
    }
}

/*
 * Any interrupt i that is not accessible to S-mode or U-Mode
 * appears as hard-wired zeros in clicintip[i], clicintie[i],
 * clicintattr[i], and clicintctl[i].(Section 3.9)(Section 3.10)
 */
static bool
riscv_clic_check_visible(RISCVCLICState *clic, int mode, int irq)
{
    uint8_t intattr_mode = get_field(clic->clicintattr[irq],
                                     CLIC_INTATTR_MODE);

    if (!clic->prv_s && !clic->prv_u) { /* M */
        return mode == PRV_M;
    } else if (clic->prv_s && clic->prv_u) { /* M/S/U */
        switch (clic->nmbits) {
        case 0:
            return mode == PRV_M;
        case 1:
            return (mode == PRV_M) || (intattr_mode <= PRV_S);
        case 2:
            return mode >= intattr_mode;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                "clic: nmbits can only be 0 or 1 or 2 for M/S/U hart");
            exit(1);
        }
    } else { /* M/S or M/U */
        switch (clic->nmbits) {
        case 0:
            return mode == PRV_M;
        case 1:
            return (mode == PRV_M) || (intattr_mode <= PRV_S);
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                "clic: nmbits can only be 0 or 1 for M/S or M/U hart");
            exit(1);
        }
    }
    return false;
}

/*
 * For level-triggered interrupts, software writes to pending bits are
 * ignored completely. (Section 3.4)
 */
static bool
riscv_clic_validate_intip(RISCVCLICState *clic, int irq)
{
    return riscv_clic_is_edge_triggered(clic, irq);
}

static void
riscv_clic_update_intip(RISCVCLICState *clic, int irq, uint64_t value)
{
    clic->clicintip[irq] = !!value;
    riscv_clic_next_interrupt(clic);
}

/*
 * For security purpose, the field can only be set to a privilege
 * level that is equal mode to or lower than the currently running
 * privilege level.(Section 3.6)
 */
static bool riscv_clic_validate_intattr(RISCVCLICState *clic, uint8_t value)
{
    int mode = extract64(value, CLIC_INTATTR_MODE_SHIFT,
                         CLIC_INTATTR_MODE_WIDTH);

    if (!qtest_enabled()) {
        CPURISCVState *env = current_cpu->env_ptr;
        if (env->priv < mode) {
            return false;
        }
    }
    return true;
}

/*
 * Work out the effective requested mode based on the number of nmbits.
 *
 * priv-modes nmbits mode Interpretation
 * M            0     xx  M-mode interrupt
 *
 * M/U          0     xx  M-mode interrupt
 * M/U          1     0x  U-mode interrupt
 * M/U          1     1x  M-mode interrupt
 *
 * M/S          0     xx  M-mode interrupt
 * M/S          1     0x  S-mode interrupt
 * M/S          1     1x  M-mode interrupt
 *
 * M/S/U        0     xx  M-mode interrupt
 * M/S/U        1     0x  S-mode interrupt
 * M/S/U        1     1x  M-mode interrupt
 * M/S/U        2     00  U-mode interrupt
 * M/S/U        2     01  S-mode interrupt
 * M/S/U        2     10  Reserved
 * M/S/U        2     11  M-mode interrupt
 *
 * M/S/U        3     xx  Reserved
 */
static uint8_t riscv_clic_effective_mode(RISCVCLICState *clic, uint8_t intattr)
{
    uint8_t mode = get_field(intattr, CLIC_INTATTR_MODE);

    switch (clic->nmbits) {
    case 0:
        mode = PRV_M;
        break;

    case 1:
        if (mode <= PRV_S) {
            if (clic->prv_s) {
                mode = PRV_S;
            } else {
                assert(clic->prv_u);
                mode = PRV_U;
            }
        } else {
            mode = PRV_M;
        }
        break;

    case 2:
        /* no modification required */
        break;

    default:
        /* We validate nmbits so this shouldn't be possible */
        assert(clic->nmbits <= 2);
    }

    return mode;
}

/* Return target interrupt number */
static int riscv_clic_get_irq(RISCVCLICState *clic, hwaddr addr)
{
    return addr / 4;
}

/* Encode the priority and IRQ as a single sortable value */
static inline int riscv_clic_encode_priority(const CLICActiveInterrupt *i)
{
    /* Highest mode+level+priority */
    int priority = (i->intcfg & CLIC_INTCFG_MASK) << CLIC_IRQ_BITS;
    /* Highest irq number */
    int irq = i->irq & CLIC_IRQ_MASK;
    /* Combined */
    return priority | irq;
}

static int riscv_clic_active_compare(const void *a, const void *b)
{
    return riscv_clic_encode_priority(b) - riscv_clic_encode_priority(a);
}

static void
riscv_clic_update_intie(RISCVCLICState *clic, int mode,
                        int irq, uint64_t new_intie)
{
    CLICActiveInterrupt *active_list = clic->active_list;

    uint8_t old_intie = clic->clicintie[irq];
    clic->clicintie[irq] = !!new_intie;

    /* Add to or remove from list of active interrupts */
    if (new_intie && !old_intie) {
        uint16_t intcfg = (mode << CLIC_INTCFG_MODE_SHIFT) |
                           clic->clicintctl[irq];
        active_list[clic->active_count].intcfg = intcfg;
        active_list[clic->active_count].irq = irq;
        clic->active_count++;
    } else if (!new_intie && old_intie) {
        CLICActiveInterrupt key = {
            (mode << 8) | clic->clicintctl[irq], irq
        };
        CLICActiveInterrupt *result = bsearch(&key,
                                              active_list, clic->active_count,
                                              sizeof(CLICActiveInterrupt),
                                              riscv_clic_active_compare);
        assert(result);
        size_t elem = result - active_list;
        size_t sz = --clic->active_count - elem;
        memmove(&result[0], &result[1], sz);
    }

    /* Sort list of active interrupts */
    qsort(active_list, clic->active_count,
          sizeof(CLICActiveInterrupt),
          riscv_clic_active_compare);

    riscv_clic_next_interrupt(clic);
}

static void
riscv_clic_hart_write(RISCVCLICState *clic, hwaddr addr,
                      uint64_t value, unsigned size,
                      int mode, int irq)
{
    int req = extract32(addr, 0, 2);

    /* visibility is checked in riscv_clic_write */

    if (irq >= clic->num_sources) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid irq %u: 0x%" HWADDR_PRIx "\n", irq, addr);
        return;
    }

    switch (req) {
    case 0: /* clicintip[i] */
        if (riscv_clic_validate_intip(clic, irq)) {
            /*
             * The actual pending bit is located at bit 0 (i.e., the
             * least significant bit). In case future extensions expand the bit
             * field, from FW perspective clicintip[i]=zero means no interrupt
             * pending, and clicintip[i]!=0 (not just 1) indicates an
             * interrupt is pending. (Section 3.4)
             */
            if (value != clic->clicintip[irq]) {
                riscv_clic_update_intip(clic, irq, value);
            }
        }
        /* Handle a 32-bit write */
        if (size > 1) {
            unsigned width = MIN(size, 4);
            unsigned i;
            for (i = 1; i < width; i++) {
                uint64_t local_value = (value >> (i * 8)) & 0xFF;
                riscv_clic_hart_write(clic, addr + i, local_value,
                                      1, mode, irq);
            }
        }
        break;

    case 1: /* clicintie[i] */
        if (clic->clicintie[irq] != value) {
            riscv_clic_update_intie(clic, mode, irq, value);
        }
        break;

    case 2: /* clicintattr[i] */
        uint8_t field_mode = riscv_clic_effective_mode(clic, value);
        if (PRV_H == field_mode) {
            field_mode = get_field(clic->clicintattr[irq],
                                   CLIC_INTATTR_MODE);
        }
        value = set_field(value, CLIC_INTATTR_MODE, field_mode);
        if (riscv_clic_validate_intattr(clic, value)) {
            if (clic->clicintattr[irq] != value) {
                clic->clicintattr[irq] = value;
                riscv_clic_next_interrupt(clic);
            }
        }
        break;

    case 3: /* clicintctl[i] */
        if (value != clic->clicintctl[irq]) {
            clic->clicintctl[irq] = value;
            riscv_clic_next_interrupt(clic);
        }
        break;
    }
}

static uint64_t
riscv_clic_hart_read(RISCVCLICState *clic, hwaddr addr, unsigned size,
                     int mode, int irq)
{
    int req = extract32(addr, 0, 2);
    int i;

    /* visibility is checked in riscv_clic_read */

    if (irq >= clic->num_sources) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid irq %u: 0x%" HWADDR_PRIx "\n", irq, addr);
        return 0;
    }

    switch (req) {
    case 0: /* clicintip[i] */
        uint64_t retval = clic->clicintip[irq];
        if (size > 1) {
            /* Handle a multi-part read */
            for (i = 1; i < size; ++i) {
                uint64_t subval =
                    (riscv_clic_hart_read(clic, addr + i, 1, mode, irq) & 0xFF);
                retval |= subval << (i * 8);
            }
        }
        return retval;

    case 1: /* clicintie[i] */
        return clic->clicintie[irq];
    case 2: /* clicintattr[i] */
        /*
         * clicintattr register layout
         * Bits Field
         * 7:6 mode
         * 5:3 reserved (WPRI 0)
         * 2:1 trig
         * 0 shv
         */
        uint8_t intattr = clic->clicintattr[irq] & CLIC_INTATTR_MASK;
        int field_mode = riscv_clic_effective_mode(clic, intattr);
        intattr = set_field(intattr, CLIC_INTATTR_MODE, field_mode);
        return intattr;

    case 3: /* clicintctl[i] */
        /*
         * The implemented bits are kept left-justified in the most-significant
         * bits of each 8-bit clicintctl[i] register, with the lower
         * unimplemented bits treated as hardwired to 1.(Section 3.7)
         */
        return clic->clicintctl[irq] |
               ((1 << (8 - clic->clicintctlbits)) - 1);
    }

    return 0;
}

static void
riscv_clic_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    RISCVCLICView *clicview = opaque;
    RISCVCLICState *clic = clicview->clic;
    CPUState *cpu = cpu_by_arch_id(clic->hartid);
    CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
    hwaddr clic_size = clic->clic_size;
    int mode = clicview->mode, irq;
/*
 * The Codasip CLIC doesn't use multiple Apertures for the memory mappeed regs
 * Instead it uses the CPU privilege mode to ensure the access only touches
 * the supported bits.
 * Setting the aperture view mode to match the current privilege level
 * should have the same effect when we only have the 1 aperture
 */
#if defined(CODASIP_CLIC)
    mode = env->priv;
#endif
    const char *current_mode_str = (PRV_M == env->priv) ? "PRV_M" :
                                   (PRV_S == env->priv) ? "PRV_S" :
                                   (PRV_U == env->priv) ? "PRV_U" :
                                   "unknown";
    const char *access_mode_str = (PRV_M == mode) ? "PRV_M" :
                                  (PRV_S == mode) ? "PRV_S" :
                                  (PRV_U == mode) ? "PRV_U" :
                                  "unknown";

    assert(addr < clic_size);

    if (mode > env->priv) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid write to %s CLIC registers in %s mode\n",
                      access_mode_str, current_mode_str);
        return;
    }

    if (addr < CLIC_INTCTL_BASE) {
        assert(addr % 4 == 0);
        int index = addr / 4;
        switch (index) {
        case 0: /* cliccfg */
            {
                uint8_t mnlbits = extract32(value, 0, 4);
                uint8_t nmbits = extract32(value, 4, 2);
                uint8_t snlbits = extract32(value, 16, 4);
                uint8_t unlbits = extract32(value, 24, 4);

                /*
                 * The 4-bit cliccfg.mnlbits WARL field.
                 * Valid values are 0—8.
                 */
                if (mnlbits <= 8 && PRV_M == mode) {
                    clic->mnlbits = mnlbits;
                }
                if (clic->prv_s && snlbits <= 8 && mode >= PRV_S) {
                    clic->snlbits = snlbits;
                }
                if (clic->prv_u && unlbits <= 8) {
                    clic->unlbits = unlbits;
                }

                /*
                 * The nmbits field - the number of bits for the mode.
                 * Valid values are given by implemented privileges.
                 * This is only accessible in PRV_M.
                 */
                if (PRV_M == mode) {
                    if (clic->prv_s && clic->prv_u) {
                        if (nmbits <= 2) {
                            clic->nmbits = nmbits;
                        }
                    } else if (clic->prv_s || clic->prv_u) {
                        if (nmbits <= 1) {
                            clic->nmbits = nmbits;
                        }
                    } else {
                        if (nmbits == 0) {
                            clic->nmbits = 0;
                        }
                    }
                }

                break;
            }
        case CLIC_INTTRIG_START ... CLIC_INTTRIG_END: /* clicinttrig */
            {
                uint32_t interrupt_number = value & CLIC_INTTRIG_IRQN;
                if (interrupt_number <= clic->num_sources) {
                    value &= CLIC_INTTRIG_MASK;
                    clic->clicinttrig[index - CLIC_INTTRIG_START] = value;
                    /* TODO: How does this cause the interrupt to trigger? */
                }
                break;
            }
        case 2: /* mintthresh - only in CLIC spec v0.8 */
            if (0 == strcmp(clic->version, "v0.8")) {
                clic->mintthresh = value;
                break;
            }
            qemu_log_mask(LOG_GUEST_ERROR,
                          "clic: invalid write addr: 0x%" HWADDR_PRIx "\n",
                          addr);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "clic: invalid write addr: 0x%" HWADDR_PRIx "\n",
                          addr);
            return;
        }
    } else {
        addr -= CLIC_INTCTL_BASE;
        irq = riscv_clic_get_irq(clic, addr);

        if (riscv_clic_check_visible(clic, mode, irq)) {
            riscv_clic_hart_write(clic, addr, value, size, mode, irq);
        }
    }
}

static uint64_t
riscv_clic_read(void *opaque, hwaddr addr, unsigned size)
{
    RISCVCLICView *clicview = opaque;
    RISCVCLICState *clic = clicview->clic;
    CPUState *cpu = cpu_by_arch_id(clic->hartid);
    CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
    hwaddr clic_size = clic->clic_size;
    int mode = clicview->mode, irq;
#if defined(CODASIP_CLIC)
    mode = env->priv;
#endif

    assert(addr < clic_size);

    if (mode > env->priv) {
        const char *current_mode_str = (PRV_M == env->priv) ? "PRV_M" :
                                       (PRV_S == env->priv) ? "PRV_S" :
                                       (PRV_U == env->priv) ? "PRV_U" :
                                       "unknown";
        const char *access_mode_str = (PRV_M == mode) ? "PRV_M" :
                                      (PRV_S == mode) ? "PRV_S" :
                                      (PRV_U == mode) ? "PRV_U" :
                                      "unknown";
        qemu_log_mask(LOG_GUEST_ERROR,
                      "clic: invalid write to %s CLIC registers in %s mode\n",
                      access_mode_str, current_mode_str);
        return 0;
    }

    if (addr < CLIC_INTCTL_BASE) {
        assert(addr % 4 == 0);
        int index = addr / 4;
        switch (index) {
        case 0:
            /*
             * cliccfg register layout
             *
             * Bits     Field
             * 31:28    reserved (WPRI 0)
             * 27:24    unlbits
             * 23:20    reserved (WPRI 0)
             * 19:16    snlbits
             * 15:6     reserved (WPRI 0)
             *  5:4     nmbits
             *  3:0     mnlbits
             */
            uint64_t cliccfg = 0;
            if (PRV_M == mode) {
                cliccfg = clic->mnlbits | (clic->nmbits << 4);
            }
            if (clic->prv_s && mode >= PRV_S) {
                cliccfg |= clic->snlbits << 16;
            }
            if (clic->prv_u && mode >= PRV_U) {
                cliccfg |= clic->unlbits << 24;
            }
            return cliccfg;

        case CLIC_INTTRIG_START ... CLIC_INTTRIG_END: /* clicinttrig */
            /*
             * clicinttrig register layout
             *
             * Bits Field
             * 31 enable
             * 30:13 reserved (WARL 0)
             * 12:0 interrupt_number
             */
            uint64_t inttrig = clic->clicinttrig[index - CLIC_INTTRIG_START];
            return inttrig & CLIC_INTTRIG_MASK;

        case 2: /* mintthresh - only in CLIC spec v0.8 */
            if (0 == strcmp(clic->version, "v0.8")) {
                return clic->mintthresh;
                break;
            }
            qemu_log_mask(LOG_GUEST_ERROR,
                          "clic: invalid read : 0x%" HWADDR_PRIx "\n",
                          addr);
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "clic: invalid read : 0x%" HWADDR_PRIx "\n",
                          addr);
            break;
        }
    } else {
        addr -= CLIC_INTCTL_BASE;
        irq = riscv_clic_get_irq(clic, addr);

        if (riscv_clic_check_visible(clic, mode, irq)) {
            return riscv_clic_hart_read(clic, addr, size, mode, irq);
        }
    }

    return 0;
}

static void riscv_clic_set_irq(void *opaque, int id, int level)
{
    RISCVCLICState *clic = opaque;
    TRIG_TYPE type;

    type = riscv_clic_get_trigger_type(clic, id);

    /*
     * In general, the edge-triggered interrupt state should be kept in pending
     * bit, while the level-triggered interrupt should be kept in the level
     * state of the incoming wire.
     *
     * For CLIC, model the level-triggered interrupt by read-only pending bit.
     */
    if (level) {
        switch (type) {
        case POSITIVE_LEVEL:
        case POSITIVE_EDGE:
            riscv_clic_update_intip(clic, id, level);
            break;
        case NEG_LEVEL:
            riscv_clic_update_intip(clic, id, !level);
            break;
        case NEG_EDGE:
            break;
        default:
            /* It's a 2-bit field so this shouldn't be possible */
            assert(type <= 3);
        }
    } else {
        switch (type) {
        case POSITIVE_LEVEL:
            riscv_clic_update_intip(clic, id, level);
            break;
        case POSITIVE_EDGE:
            break;
        case NEG_LEVEL:
        case NEG_EDGE:
            riscv_clic_update_intip(clic, id, !level);
            break;
        default:
            /* It's a 2-bit field so this shouldn't be possible */
            assert(type <= 3);
        }
    }
}

static void riscv_clic_cpu_irq_handler(void *opaque, int irq, int level)
{
    CPURISCVState *env = (CPURISCVState *)opaque;
    RISCVCLICState *clic = env->clic;

    if (level) {
        env->exccode = clic->exccode;
        cpu_interrupt(env_cpu(env), CPU_INTERRUPT_CLIC);
    }
}

static const MemoryRegionOps riscv_clic_ops = {
    .read = riscv_clic_read,
    .write = riscv_clic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8
    }
};

static void riscv_clic_realize(DeviceState *dev, Error **errp)
{
    RISCVCLICState *clic = RISCV_CLIC(dev);
    size_t irqs = clic->num_sources;

    if (clic->prv_s && clic->prv_u) {
        clic->nmbits = 2;
    } else if (clic->prv_s || clic->prv_u) {
        clic->nmbits = 1;
    } else {
        clic->nmbits = 0;
    }

    clic->clicintip = g_new0(uint8_t, irqs);
    clic->clicintie = g_new0(uint8_t, irqs);
    clic->clicintattr = g_new0(uint8_t, irqs);
    clic->clicintctl = g_new0(uint8_t, irqs);
    clic->active_list = g_new0(CLICActiveInterrupt, irqs);

    if (!clic->prv_s) {
        clic->snlbits = 0;
    }
    if (!clic->prv_u) {
        clic->unlbits = 0;
    }

    /* Allocate irqs through gpio, so that we can use qtest */
    qdev_init_gpio_in(dev, riscv_clic_set_irq, irqs);
    qdev_init_gpio_out(dev, &clic->cpu_irq, 1);

    assert(cpu_exists(clic->hartid));
    RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(clic->hartid));
    qemu_irq irq = qemu_allocate_irq(riscv_clic_cpu_irq_handler, &cpu->env, 1);
    qdev_connect_gpio_out(dev, 0, irq);
    cpu->env.clic = clic;
}

static void riscv_clic_view_realize(DeviceState *dev, Error **errp)
{
    RISCVCLICView *clicview = RISCV_CLIC_VIEW(dev);
    RISCVCLICState *clic = clicview->clic;

    memory_region_init_io(&clicview->mmio, OBJECT(clicview), &riscv_clic_ops,
                          clicview, TYPE_RISCV_CLIC_VIEW, clic->clic_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(clicview), &clicview->mmio);
}

static Property riscv_clic_properties[] = {
    DEFINE_PROP_BOOL("shv-enabled", RISCVCLICState, shv_enabled, true),
    DEFINE_PROP_BOOL("jump-table", RISCVCLICState, jump_table, false),
    DEFINE_PROP_UINT8("mnlbits", RISCVCLICState, mnlbits, 8),
    DEFINE_PROP_UINT8("snlbits", RISCVCLICState, snlbits, 8),
    DEFINE_PROP_UINT8("unlbits", RISCVCLICState, unlbits, 8),
    DEFINE_PROP_INT32("hartid", RISCVCLICState, hartid, 0),
    DEFINE_PROP_UINT32("num-sources", RISCVCLICState, num_sources, 0),
    DEFINE_PROP_UINT32("clic-size", RISCVCLICState, clic_size, 0),
    DEFINE_PROP_UINT32("clicintctlbits", RISCVCLICState, clicintctlbits, 0),
    DEFINE_PROP_STRING("version", RISCVCLICState, version),
    DEFINE_PROP_END_OF_LIST(),
};

static Property riscv_clic_view_properties[] = {
    DEFINE_PROP_LINK("clic", RISCVCLICView, clic,
                     TYPE_RISCV_CLIC, RISCVCLICState *),
    DEFINE_PROP_UINT8("mode", RISCVCLICView, mode, PRV_U),
    DEFINE_PROP_UINT64("clicbase", RISCVCLICView, clicbase, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_clic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = riscv_clic_realize;
    device_class_set_props(dc, riscv_clic_properties);
}

static void riscv_clic_view_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = riscv_clic_view_realize;
    device_class_set_props(dc, riscv_clic_view_properties);
}

static const TypeInfo riscv_clic_info = {
    .name          = TYPE_RISCV_CLIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVCLICState),
    .class_init    = riscv_clic_class_init,
};

static const TypeInfo riscv_clic_view_info = {
    .name = TYPE_RISCV_CLIC_VIEW,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVCLICView),
    .class_init = riscv_clic_view_init,
};

static void riscv_clic_register_types(void)
{
    type_register_static(&riscv_clic_info);
    type_register_static(&riscv_clic_view_info);
}

type_init(riscv_clic_register_types)

/*
 * riscv_clic_view_create:
 *
 * @clic: machine-mode CLIC this is an view onto
 * @clicbase: base address of this view CLIC memory-mapped registers
 * @mode: the mode of the view - PRV_S or PRV_U
 *
 * Returns: the new view
 */
static RISCVCLICView *riscv_clic_view_create(RISCVCLICState *clic,
                                             hwaddr clicbase, int mode)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_CLIC_VIEW);
    RISCVCLICView *clicview = RISCV_CLIC_VIEW(dev);
    Object *obj = OBJECT(dev);
    Object *clicobj = OBJECT(clic);

    assert(0 != clic);                  /* this should exist */
    assert(0 != clicbase);              /* this should exist */
    assert(0 == (clicbase & 0xfff));    /* base should be 4KiB-aligned */
    assert(PRV_M == mode || PRV_S == mode || PRV_U == mode);

    object_property_add_child(clicobj, modeview_name[mode], obj);
    clicview->clic = clic;

    qdev_prop_set_uint8(dev, "mode", mode);
    qdev_prop_set_uint64(dev, "clicbase", clicbase);

    if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal)) {
        object_unparent(obj);
        return NULL;
    }

    memory_region_init_io(&clicview->mmio, OBJECT(dev), &riscv_clic_ops,
                          clicview, TYPE_RISCV_CLIC_VIEW, clic->clic_size);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, clicbase);

    return clicview;
}

/*
 * riscv_clic_create:
 *
 * @mclicbase: base address of PRV_M CLIC memory-mapped registers
 * @sclicbase: base address of PRV_S CLIC memory-mapped registers
 * @uclicbase: base address of PRV_U CLIC memory-mapped registers
 * @hartid: the HART ID this CLIC is serving
 * @num_sources: number of interrupts supporting by each aperture
 * @clicintctlbits: bits are actually implemented in the clicintctl registers
 * @version: clic version, such as "v0.9"; append "-jmp" for jump table instead
 *           of function pointers
 *
 * Returns: the device object
 */
DeviceState *riscv_clic_create(hwaddr mclicbase, hwaddr sclicbase,
                               hwaddr uclicbase, uint32_t hartid,
                               uint32_t num_sources, uint8_t clicintctlbits,
                               const char *version)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_CLIC);
    RISCVCLICState *s = RISCV_CLIC(dev);
    g_autofree char **tokens = NULL;
    char *base_version;
    bool jump_table = false;

    assert(num_sources <= CLIC_MAX_IRQ_COUNT);
    assert(cpu_exists(hartid));
    assert(clicintctlbits <= MAX_CLIC_INTCTLBITS);
    assert(0 == (mclicbase & 0xfff));    /* base should be 4KiB-aligned */

    /* Parse the version */
    tokens = g_strsplit(version, "-", 2);
    base_version = g_strdup(tokens[0]);
    assert(0 == strcmp(base_version, "v0.9"));
    if (tokens[1]) {
        assert(0 == strcmp(tokens[1], "jmp"));
        jump_table = true;
    }

    qdev_prop_set_uint32(dev, "hartid", hartid);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    qdev_prop_set_uint32(dev, "clic-size", num_sources * 4 + CLIC_INTCTL_BASE);
    qdev_prop_set_uint32(dev, "clicintctlbits", clicintctlbits);
    qdev_prop_set_string(dev, "version", base_version);
    qdev_prop_set_bit(dev, "jump-table", jump_table);

    s->prv_m = riscv_clic_view_create(s, mclicbase, PRV_M);
#ifdef CODASIP_CLIC
    /*
     * The Codasip CLIC doesn't support multiple regions instead a single region
     * is used and the privilege mode used to restrict access. The prv_s and
     * prv_m pointers are still used to determine what access types are
     * supported
     */
    if (sclicbase) {
        s->prv_s = s->prv_m;
    }
    if (uclicbase) {
        s->prv_u = s->prv_m;
    }
#else
    if (sclicbase) {
        s->prv_s = riscv_clic_view_create(s, sclicbase, PRV_S);
    }
    if (uclicbase) {
        s->prv_u = riscv_clic_view_create(s, uclicbase, PRV_U);
    }
#endif
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    return dev;
}

void riscv_clic_get_next_interrupt(void *opaque)
{
    RISCVCLICState *clic = opaque;
    riscv_clic_next_interrupt(clic);
}

bool riscv_clic_shv_interrupt(void *opaque, int irq)
{
    RISCVCLICState *clic = opaque;
    return riscv_clic_is_shv_interrupt(clic, irq);
}

bool riscv_clic_edge_triggered(void *opaque, int irq)
{
    RISCVCLICState *clic = opaque;
    return riscv_clic_is_edge_triggered(clic, irq);
}

bool riscv_clic_use_jump_table(void *opaque)
{
    RISCVCLICState *clic = opaque;
    return clic->jump_table;
}

void riscv_clic_clean_pending(void *opaque, int irq)
{
    RISCVCLICState *clic = opaque;
    clic->clicintip[irq] = 0;
}

/*
 * The new CLIC interrupt-handling mode is encoded as a new state in
 * the existing WARL xtvec register, where the low two bits are 11.
 */
bool riscv_clic_is_clic_mode(CPURISCVState *env)
{
    target_ulong xtvec = (env->priv == PRV_M) ?
        GET_SPECIAL_REG_ARCH(env, mtvec, mtvecc) :
        GET_SPECIAL_REG_ARCH(env, stvec, stvecc);
    return env->clic && ((xtvec & XTVEC_MODE) == XTVEC_CLIC);
}

void riscv_clic_decode_exccode(uint32_t exccode, int *mode,
                               int *il, int *irq)
{
    *irq = get_field(exccode, RISCV_EXCP_CLIC_IRQ);
    *mode = get_field(exccode, RISCV_EXCP_CLIC_MODE);
    *il = get_field(exccode, RISCV_EXCP_CLIC_LEVEL);
}

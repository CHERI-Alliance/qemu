/*
 * Codasip Timer
 *
 * Copyright (c) 2024 Codasip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Authors:
 *   Axel Heider <axel.heider@codasip.com>
 *
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "hw/irq.h"
#include "hw/timer/codasip-timer.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

/*
 * The timer is basically the timer used in the Codasip ACLINT. Thus it just has
 * two registers. It is very simple and can't be disabled, The interrupt is
 * asserted when TIME >= CMP. Since TIME starts at 0 and CMP is initialized with
 * 0, the timer interrupt gets asserted immediately when the system starts.
 * Software can set CMP to UINT64_MAX to silence the interrupt, this practically
 * disables the timer. Software can write TIME to set a custom epoch.
 */
REG64(CMP,  0x0000)
REG64(TIME, 0x7ff8)

#define CODASIP_TIMER_MEMORY_SIZE  0x8000

/* The timer runs at 100 MHz by default. */
#define CODASIP_TIMER_FRQ_HZ    100000000ULL


static inline uint64_t codasip_timer_ns_to_ticks(CodasipTimerState_t *s,
                                                uint64_t time_ns)
{
    return muldiv64(time_ns, s->freq_hz, NANOSECONDS_PER_SECOND);
}

static inline uint64_t codasip_timer_ticks_to_ns(CodasipTimerState_t *s,
                                                uint64_t ticks)
{
    return muldiv64(ticks, NANOSECONDS_PER_SECOND, s->freq_hz);
}

static uint64_t codasip_timer_get_qemu_time_ns(void *opaque)
{
    uint64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    return now_ns;
}

static void codasip_timer_interrupt(void *opaque)
{
    CodasipTimerState_t *s = opaque;

    qemu_irq_raise(s->irq);

    uint64_t now_ns = codasip_timer_get_qemu_time_ns(s);
    trace_codasip_timer_interrupt(now_ns);
}

static void codasip_timer_update(CodasipTimerState_t *s)
{
    uint64_t cmp_ticks = s->cmp_ticks;

    /*
     * Assume the new timeout is in the future and deassert the interrupt. For
     * any values in the past we get a QEMU timer interrupt immediately, which
     * will assert the interrupt.
     */
    qemu_irq_lower(s->irq);

    /*
     * The QEMU timer API takes a signed 64-bit integers. The value "-1"
     * indicates an infinite timeout. All other negative values are turned into
     * zero, so the timer will trigger immediately.
     */
    uint64_t alarm_ns = INT64_MAX;
    uint64_t cmp_rel_ns = codasip_timer_ticks_to_ns(s, cmp_ticks);
    uint64_t cmp_ns = s->offset_ns + cmp_rel_ns;
    if ((cmp_ns >= s->offset_ns) && (cmp_ns < INT64_MAX)) {
        /* no overflow after applying offset and in representable range */
        alarm_ns = cmp_ns;
    }

    timer_mod_anticipate_ns(&s->timer, alarm_ns);
    trace_codasip_timer_update(alarm_ns);
}

static void codasip_timer_write_CMP(CodasipTimerState_t *s, uint64_t cmp_ticks)
{
    s->cmp_ticks = cmp_ticks;
    codasip_timer_update(s);
}

static void codasip_timer_write_TIME(CodasipTimerState_t *s,
                                     uint64_t time_ticks)
{
    uint64_t time_ns = codasip_timer_ticks_to_ns(s, time_ticks);
    uint64_t now_ns = codasip_timer_get_qemu_time_ns(s);
    s->offset_ns = now_ns - time_ns;
    codasip_timer_update(s);
}

static void codasip_timer_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    CodasipTimerState_t *s = opaque;

    trace_codasip_timer_write(addr, size, val64);

    switch (addr) {
    case A_CMP:
        codasip_timer_write_CMP(s, val64);
        return;
    case A_TIME:
        codasip_timer_write_TIME(s, val64);
        return;
    default:
        break;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
}

static uint64_t codasip_timer_read_CMP(CodasipTimerState_t *s)
{
    uint64_t ticks = s->cmp_ticks;
    return ticks;
}

static uint64_t codasip_timer_read_TIME(CodasipTimerState_t *s)
{
    uint64_t now_ns = codasip_timer_get_qemu_time_ns(s);
    uint64_t time_ns = now_ns - s->offset_ns;
    uint64_t time_ticks = codasip_timer_ns_to_ticks(s, time_ns);
    return time_ticks;
}

static uint64_t codasip_timer_read(void *opaque, hwaddr addr, unsigned int size)
{
    CodasipTimerState_t *s = opaque;

    trace_codasip_timer_read(addr, size);

    switch (addr) {
    case A_CMP:
        return codasip_timer_read_CMP(s);
    case A_TIME:
        return codasip_timer_read_TIME(s);
    default:
        break;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    return 0;
}

static void codasip_timer_reset(DeviceState *dev)
{
    CodasipTimerState_t *s = CODASIP_TIMER(dev);

    /* s->freq_hz was initialized by (default) properties */
    s->cmp_ticks = 0;

    /* We expect to read 0 here when QEMU starts. */
    uint64_t now_ns = codasip_timer_get_qemu_time_ns(s);
    s->offset_ns = now_ns;

    codasip_timer_update(s);
}

static void codasip_timer_realize(DeviceState *dev, Error **errp)
{
    CodasipTimerState_t *s = CODASIP_TIMER(dev);

    /* Activate the virtual hardware. */
    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, codasip_timer_interrupt, s);
}

static const MemoryRegionOps codasip_timer_ops = {
    .read = codasip_timer_read,
    .write = codasip_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    /*
     * This simple timer has 2 registers that supports 64-bit accesses only for
     * now for now, becuase currently we are targeting 64-bit platform. For a
     * 32-bit platform we have to implement the 32-bit accesses eventually, but
     * that is currently not a priority.
     * Seems QEMU defaults to 32-bit accesses. It splits up 64-bit accesses into
     * two 32-bit accesses, even if we are on 64-bit architecture, unless we
     * explicitly state we support 64-bit accesses.
     */
    .valid = {
        /*
         * According to the QEMU documentation, these are the guest-visible
         * constraints. We support 64-bit accesses only, any other access causes
         * a machine check to be chrown.
         */
        .min_access_size = 8,
        .max_access_size = 8
    },
    .impl = {
        /*
         * According to the QEMU documentation, these are the internal
         * implementation constraints. Accesses of smaller sizes will be rounded
         * upwards and a partial result will be returned. Since we currently
         * support 64-bit accesses only for now form the setting above, any
         * setting here is practicall ignored and just added for reference
         */
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static void codasip_timer_init(Object *obj)
{
    CodasipTimerState_t *s = CODASIP_TIMER(obj);

    /* Instanciate the virtual hardware. */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &codasip_timer_ops, s,
                          TYPE_CODASIP_TIMER, CODASIP_TIMER_MEMORY_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription vmstate_codasip_timer = {
    .name = TYPE_CODASIP_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER(timer, CodasipTimerState_t),
        VMSTATE_UINT64(freq_hz, CodasipTimerState_t),
        VMSTATE_UINT64(offset_ns, CodasipTimerState_t),
        VMSTATE_UINT64(cmp_ticks, CodasipTimerState_t),
        VMSTATE_END_OF_LIST()
    }
};

static Property codasip_timer_properties[] = {
    /* default to 100 MHz */
    DEFINE_PROP_UINT64("clock-frequency", CodasipTimerState_t,
                       freq_hz, CODASIP_TIMER_FRQ_HZ),
    DEFINE_PROP_END_OF_LIST(),
};

static void codasip_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_codasip_timer;
    /*
     * deprecated, modern code should use the ResettableClass interface to
     * implement a multi-phase reset.
     */
    dc->reset = codasip_timer_reset;
    /* called after instance_init() */
    dc->realize = codasip_timer_realize;
    device_class_set_props(dc, codasip_timer_properties);
}

static const TypeInfo codasip_timer_info = {
    .name          = TYPE_CODASIP_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CodasipTimerState_t),
    .instance_init = codasip_timer_init,
    .class_init    = codasip_timer_class_init,
};

static void codasip_timer_register_types(void)
{
    type_register_static(&codasip_timer_info);
}

type_init(codasip_timer_register_types)

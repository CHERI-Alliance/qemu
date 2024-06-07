/*
 * Codasip Timer
 *
 * Copyright (c) 2024 Codasip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Authors:
 *   Axel Heider <axel.heider@codasip.com>
 */

#ifndef HW_CODASIP_TIMER_H
#define HW_CODASIP_TIMER_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_CODASIP_TIMER "codasip,timer"

#define CODASIP_TIMER(obj) \
    OBJECT_CHECK(CodasipTimerState_t, (obj), TYPE_CODASIP_TIMER)


typedef struct CodasipTimerState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;
    qemu_irq irq;
    QEMUTimer timer;
    uint64_t freq_hz;
    uint64_t offset_ns;
    uint64_t cmp_ticks;
} CodasipTimerState_t;

#endif /* HW_CODASIP_TIMER_H */

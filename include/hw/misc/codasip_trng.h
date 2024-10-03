/*
 * Codasip TRNG (True Random Number Generator) emulation
 *
 * Copyright 2024 Codasip GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CODASIP_TRNG_H
#define CODASIP_TRNG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_CODASIP_TRNG "codasip-trng"
OBJECT_DECLARE_SIMPLE_TYPE(CodasipTRNGState, CODASIP_TRNG)

struct CodasipTRNGState {
    SysBusDevice busdev;

    MemoryRegion iomem;

    uint32_t data;
    uint32_t fifo_samples;
    uint32_t config;
};

#endif /* CODASIP_TRNG_H */

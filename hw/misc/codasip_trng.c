/*
 * Codasip TRNG (True Random Number Generator) emulation
 *
 * Copyright 2024 Codasip GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/guest-random.h"
#include "migration/vmstate.h"

#include "hw/misc/codasip_trng.h"

#define TRNG_RAW        0x0     /* RAW TERO data */
#define TRNG_RAWN       0x4     /* Number of RAW samples in the FIFO */
#define TRNG_RND        0x8     /* Processed TERO data */
#define TRNG_RNDN       0xC     /* Number of processed samples in the FIFO */
#define TRNG_CONFIG     0x10    /* Hardware configuration */
#define TRNG_STATUS     0x14    /* The error count */
#define TRNG_REGS_SIZE  0x18

/*
 * To keep things simple we only emulate a one element FIFO, and share it
 * between the raw and processed FIFOs.
 */

static void trng_refresh(CodasipTRNGState *s)
{
    uint32_t data;

    if (s->fifo_samples) {
        return;
    }

    if (qemu_guest_getrandom(&data, sizeof(data), NULL) == 0) {
        s->data = data;
        s->fifo_samples = 1;
    }
}

static uint64_t trng_read(void *opaque, hwaddr offset, unsigned size)
{
    CodasipTRNGState *s = opaque;
    uint64_t value = 0;

    trng_refresh(s);

    switch (offset) {
    case TRNG_RAW:
    case TRNG_RND:
        value = s->data;
        s->fifo_samples = 0;
        break;

    case TRNG_RAWN:
    case TRNG_RNDN:
        value = s->fifo_samples;
        break;

    case TRNG_CONFIG:
        value = s->config;
        break;

    case TRNG_STATUS:
        value = 0;      /* No errors */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, offset);
        break;
    }

    return value;
}

static void trng_write(void *opaque, hwaddr offset, uint64_t value,
                       unsigned size)
{
    CodasipTRNGState *s = opaque;

    switch (offset) {
    case TRNG_RAW:
    case TRNG_RND:
    case TRNG_RAWN:
    case TRNG_RNDN:
    case TRNG_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register @ 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, offset);
        break;

    case TRNG_CONFIG:
        s->config = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, offset);
        break;
    }
}

static const MemoryRegionOps trng_ops = {
    .read = trng_read,
    .write = trng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void trng_reset(DeviceState *dev)
{
    CodasipTRNGState *s = CODASIP_TRNG(dev);

    s->fifo_samples = 0;
    s->config = 0;
}

static void trng_init(Object *obj)
{
    CodasipTRNGState *s = CODASIP_TRNG(obj);

    memory_region_init_io(&s->iomem, obj, &trng_ops, s, "regs",
                          TRNG_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static const VMStateDescription vmstate_trng = {
    .name = TYPE_CODASIP_TRNG,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(data, CodasipTRNGState),
        VMSTATE_UINT32(fifo_samples, CodasipTRNGState),
        VMSTATE_UINT32(config, CodasipTRNGState),
        VMSTATE_END_OF_LIST(),
    },
};

static void trng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Codasip True Random Number Generator";
    dc->reset = trng_reset;
    dc->vmsd = &vmstate_trng;
}

static const TypeInfo trng_types[] = {
    {
        .name = TYPE_CODASIP_TRNG,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(CodasipTRNGState),
        .class_init = trng_class_init,
        .instance_init = trng_init,
    },
};
DEFINE_TYPES(trng_types);

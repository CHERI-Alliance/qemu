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

#include "qemu/osdep.h"
#include "hw/riscv/cmu.h"
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#ifdef TARGET_CHERI
#include "cheri_tagmem.h"
#endif

static uint64_t cmu_read(void *opaque, hwaddr addr, unsigned int size)
{
    CMUDeviceState *s = opaque;
    assert(addr + size <= 0x10000);
    assert(size <= 8);
    if (addr + size > CMU_REGS_SIZE) {
        // attempting to read from filter table or memory window
        // not implemented
        return 0;
    }
    uint64_t retval = 0;
    uint8_t *ptr = (uint8_t *)&retval;
        uint8_t *regptr = ((uint8_t *)&s->regs) + addr;
    while (size--) {
        *ptr++ = *regptr++;
    }
    return retval;
}
// trigger an invalidation on this CMU
// Extract the region from the device state
static void cmu_invalidate(CMUDeviceState *s)
{
    CMUClass *c = CMU_DEVICE_GET_CLASS(s);
    ram_addr_t start_addr, end_addr, len;

    /*  the address field bit definition is largely based on the CLEN, and
       physical address size. Specifically bits 0-> log2(CLEN)-1 are zero Bits
       63-> CMU_PHYSICAL_ADDRESS_SIZE are zero The remaining bits are used for
       the address. These are aligned such that the physical address can be used
       as it with the low order bits zeroed to round down to the next 8
       capability granularity.
    */

    if (__builtin_sub_overflow(
                (s->regs[REG_CMU_TISTART] & ~((1 << LOG2_CMU_CLEN) - 1)),
                s->base, &start_addr)) {
        return;
    }
    if (__builtin_sub_overflow(
                (s->regs[REG_CMU_TIEND] & ~((1 << LOG2_CMU_CLEN) - 1)),
                s->base, &end_addr)) {
        return;
    }

    // End address is address of start of cap, so round up to the next 8 caps
    if (__builtin_sub_overflow(end_addr + (1 << LOG2_CMU_CLEN), start_addr, &len)) {
        return;
    }

    /*
     * start_addr is the offset into the ram region. len is the size of the
     * area we want to clear. The region's length must be >= start_addr + len.
     */
    if (qemu_ram_get_used_length(s->managed->ram_block) < (start_addr + len)) {
        return;
    }

    if (c->invalidate_region) {
        c->invalidate_region(s->managed->ram_block, start_addr, len);
    }

    // clear the activate bit.
    s->regs[REG_CMU_TIEND] = s->regs[REG_CMU_TIEND] & ~CMU_TI_ACTIVE;
}

static void cmu_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size)
{
    CMUDeviceState *s = opaque;

    assert(addr + size <= 0x10000);
    assert(size <= 8);
    if (addr + size > CMU_REGS_SIZE) {
        // attempting to write to filter table or memory window
        // not implemented
        return;
    }

    if (addr <= 0x8) {
        return; // dont write to the feature register
    }

    uint8_t *inptr = (uint8_t *)&data;
    uint8_t *regptr = ((uint8_t *)&s->regs) + addr;
    while (size--) {
        *regptr++ = *inptr++;
    }

    // after writing have a look at activate bit and trigger an invalidate if
    // required.
    if (s->regs[REG_CMU_TIEND] & CMU_TI_ACTIVE) {
        cmu_invalidate(s);
    }
}

static const MemoryRegionOps cmu_ops = {
    .read = cmu_read,
    .write = cmu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.max_access_size = 8,
    .valid.min_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,

};

static Property cmu_properties[] = {
    DEFINE_PROP_UINT64("ram-base", CMUDeviceState, base, 0),
    DEFINE_PROP_UINT64("ram-size", CMUDeviceState, size, 0),
    DEFINE_PROP_LINK("managed-ram", CMUDeviceState, managed,
            TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void cmu_instance_init(Object *obj)
{
    CMUDeviceState *s = CMU_DEVICE(obj);

    /* allocate memory map region */
    memory_region_init_io(&s->iomem, obj, &cmu_ops, s, TYPE_CMU_DEVICE,
                          CMU_REGION_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    s->regs[0] = CMU_FT_DEFAULT;
}

static void cmu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CMUClass *c = CMU_DEVICE_CLASS(oc);

    device_class_set_props(dc, cmu_properties);

#ifdef TARGET_CHERI
    c->invalidate_region = cheri_tag_phys_invalidate_external;
#else
    c->invalidate_region = NULL;
#endif
}

static const TypeInfo cmu_device_info = {
    .name = TYPE_CMU_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CMUDeviceState),
    .instance_init = cmu_instance_init,
    .class_init = cmu_class_init,
};

static void cmu_device_register_types(void)
{
    type_register_static(&cmu_device_info);
}

type_init(cmu_device_register_types)
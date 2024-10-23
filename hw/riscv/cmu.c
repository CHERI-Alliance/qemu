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
    assert(size == 8);
    assert(addr <= 0x0fffc);
    if (addr >= CMU_REGS_SIZE) {
        // attempting to read from filter table or memory window
        // not implemented
        return 0;
    }
    return s->regs[addr / sizeof(uint64_t)];
}
// trigger an invalidation on this CMU
// Extract the region from the device state
static void cmu_invalidate(CMUDeviceState *s)
{

    /*  the address field bit definition is largely based on the CLEN, and
       physical address size. Specifically bits 0-> log2(CLEN)-1 are zero Bits
       63-> CMU_PHYSICAL_ADDRESS_SIZE are zero The remaining bits are used for
       the address. These are aligned such that the physical address can be used
       as it with the low order bits zeroed to round down to the next 8
       capability granularity.
    */
    ram_addr_t start_addr =
        (s->regs[REG_CMU_TISTART] & ~((1 << LOG2_CMU_CLEN) - 1)) -
        s->base;
    ram_addr_t end_addr =
        (s->regs[REG_CMU_TIEND] & ~((1 << LOG2_CMU_CLEN) - 1)) -
        s->base;

    // End address is address of start of cap, so round up to the next 8 caps
    ram_addr_t len = (end_addr - start_addr) + (1 << LOG2_CMU_CLEN);
    if (s->invalidate_region)
        s->invalidate_region(s->managed->ram_block, start_addr, len);
    // clear the activate bit.
    s->regs[REG_CMU_TIEND] = s->regs[REG_CMU_TIEND] & ~CMU_TI_ACTIVE;
}

static void cmu_write(void *opaque, hwaddr addr, uint64_t data, unsigned int size)
{
    CMUDeviceState *s = opaque;
    assert(size == 8);
    assert(addr <= 0x0fffc);

    if(addr >= CMU_REGS_SIZE) {
        // attempting to read from filter table or memory window
        // not implemented
        return;
    }
    if (addr <= 0x8) {
        return; // dont write to the feature register
    }

    s->regs[addr / sizeof(uint64_t)] = data;
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
    .valid.min_access_size = 8,
    .impl.min_access_size = 8,

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
#ifdef TARGET_CHERI
    s->invalidate_region = cheri_tag_phys_invalidate_external;
#else
    s->invalidate_region = NULL;
#endif
}

static void cmu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, cmu_properties);
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

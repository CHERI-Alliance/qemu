/*
 * Codasip Hobgoblin Non-Volatile Emulator Block
 *
 * Copyright (c) 2024 Codasip Limited s.r.o
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
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "hw/nvram/hobgoblin_nvemu.h"
#include "hw/sysbus.h"
#include "sysemu/hostmem.h"

#define NVEMU_REG_SIZE   0x100
#define NVEMU_NVRAM_SIZE 0xC00

/* The spec indicates that the registers are 32-bit wide. */
REG32(NVEMU_STATUS, 0x00)
REG32(NVEMU_CONFIG, 0x04)

static RegisterAccessInfo nvemu_regs_info[] = {
    /* TODO: confirm status' reset value */
    { .name = "status", .addr = A_NVEMU_STATUS, .reset = 0x0, .ro = BIT_MASK(32) },
    { .name = "config", .addr = A_NVEMU_CONFIG },
};

#define NUM_NVEMU_REGS ARRAY_SIZE(nvemu_regs_info)

typedef struct NVEmuState {
    SysBusDevice parent_obj;
    MemoryRegion mem; /* all of nvemu's address space (registers, nvram) */
    HostMemoryBackend *nvram_be;
    uint32_t regs[NUM_NVEMU_REGS];
    RegisterInfo regs_info[NUM_NVEMU_REGS];
} NVEmuState;

/* A simple type will do for us, we need no class struct. */
OBJECT_DECLARE_SIMPLE_TYPE(NVEmuState, HOB_NVEMU)

static const MemoryRegionOps nvemu_reg_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_NATIVE_ENDIAN,
    /*
     * These constraints apply to register access from the guest system.
     * (impl defines constraints for qemu-internal acces to the registers)
     */
    .valid.max_access_size = 4,
    .valid.min_access_size = 1,
};

static Property nvemu_properties[] = {
    DEFINE_PROP_LINK("memdev", NVEmuState, nvram_be, TYPE_MEMORY_BACKEND,
            HostMemoryBackend *),
    DEFINE_PROP_END_OF_LIST(),
};

static void nvemu_enter_reset(Object *obj, ResetType type)
{
    NVEmuState *s = HOB_NVEMU(obj);
    uint16_t i;

    for (i = 0; i < NUM_NVEMU_REGS; i++) {
        register_reset(&s->regs_info[i]);
    }
}

static void nvemu_realize(DeviceState *dev, Error **errp)
{
    NVEmuState *s = HOB_NVEMU(dev);
    RegisterInfoArray *reg_array;
    MemoryRegion *mr_nvram;

    /*
     * Please note that the values of the properties are available here in the
     * realize function, but not in instance_init. For more details, see
     * https://people.redhat.com/~thuth/blog/qemu/2018/09/10/instance-init-realize.html
     */

    /* call ERRP_GUARD() here if we need this in future (see qapi/error.h) */

    memory_region_init(&s->mem, OBJECT(dev), TYPE_HOB_NVEMU,
            NVEMU_REG_SIZE + NVEMU_NVRAM_SIZE);

    /* parameter of host_memory_backend_get_memory must not be NULL */
    if (!s->nvram_be) {
        error_setg(errp, "no memory backend - skip this device");
        return;
    }
    mr_nvram = host_memory_backend_get_memory(s->nvram_be);

    /*
     * memory-backend-file expects the image size to be a multiple of the
     * host's page size and sets this size for the memory region. We have
     * to trim the mem region to the size of the nvram in our address space.
     */
    memory_region_set_size(mr_nvram, NVEMU_NVRAM_SIZE);

    if (host_memory_backend_is_mapped(s->nvram_be)) {
        error_setg(errp, "memory backend already in use - skip this device");
        return;
    }
    /* nvram area starts after the registers */
    memory_region_add_subregion(&s->mem, NVEMU_REG_SIZE, mr_nvram);
    host_memory_backend_set_mapped(s->nvram_be, true);

    reg_array = register_init_block32(dev,
            nvemu_regs_info, ARRAY_SIZE(nvemu_regs_info),
            s->regs_info, s->regs,
            &nvemu_reg_ops,
            /* debug enabled? */ false,
            NVEMU_REG_SIZE);

    memory_region_add_subregion(&s->mem, 0x0, &reg_array->mem);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mem);
}

static void nvemu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    /*
     * Don't allow callers to instantiate this device via -device on the qemu
     * command line. We only need one hard-wired instance for the hobgoblin
     * machine.
     *
     * Even with user_creatable = false, introspection may create another
     * instance (that is not realized).
     *
     * Generally, multiple instances *should* work if each one gets a
     * different memory backend.
     */
    dc->user_creatable = false;

    dc->realize = nvemu_realize;
    device_class_set_props(dc, nvemu_properties);

    rc->phases.enter = nvemu_enter_reset;
}

static const TypeInfo nvemu_device_info = {
    .name = TYPE_HOB_NVEMU,
    .parent = TYPE_SYS_BUS_DEVICE,

    .instance_size = sizeof(NVEmuState),
    .class_init = nvemu_class_init,
};

static void nvemu_register_types(void)
{
    type_register_static(&nvemu_device_info);
}

type_init(nvemu_register_types)

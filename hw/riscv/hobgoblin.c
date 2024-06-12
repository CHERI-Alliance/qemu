/*
 * QEMU RISC-V Hobgoblin Board
 *
 * Copyright (c) 2023 Stuart Menefy, stuart.menefy@codasip.com
 * Copyright (c) 2023 Codasip Limited
 *
 * This provides a RISC-V Board with the following devices:
 *
 * 1) CLINT (Timer and IPI)
 * 2) PLIC (Platform Level Interrupt Controller)
 * 3) 16550 UART
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
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "hw/ssi/ssi.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/intc/sifive_clint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/riscv/hobgoblin.h"
#include "hw/riscv/boot.h"
#include "hw/char/serial.h"
#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "exec/address-spaces.h"
#include "net/net.h"
#include <libfdt.h>

#define TYPE_XILINX_SPI "xlnx.xps-spi"
#define TYPE_XLNX_AXI_GPIO "xlnx.axi-gpio"
#define TYPE_XILINX_ETHLITE "xlnx.xps-ethernetlite"
#define TYPE_XILINX_AXI_ETHERNET "xlnx.axi-ethernet"
#define TYPE_XILINX_AXI_DMA "xlnx.axi-dma"

typedef struct {
    hwaddr base;
    hwaddr size;
    const char *name;
} memmapEntry_t;

static const memmapEntry_t memmap[] = {
    [HOBGOBLIN_MROM] =     {     0x1000,      0x100,
        "riscv.hobgoblin.mrom"},
    [HOBGOBLIN_BOOT_ROM] = { 0x10000000, 0x00020000,
        "riscv.hobgoblin.boot.rom"},
    [HOBGOBLIN_BOOT_RAM] = { 0x10400000, 0x00008000,
        "riscv.hobgoblin.boot.ram"},
    [HOBGOBLIN_SRAM] =     { 0x20000000, 0x08000000,
        "riscv.hobgoblin.sram"},
    [HOBGOBLIN_PLIC] =     { 0x40000000,  0x4000000, ""},
    [HOBGOBLIN_CLINT] =    { 0x60014000,     0xc000, ""},
    [HOBGOBLIN_ETHLITE] =  { 0x60020000,     0x2000, ""},
    [HOBGOBLIN_AXI_DMA] =  { 0x600a0000,    0x10000, ""},
    [HOBGOBLIN_AXI_ETH] =  { 0x600c0000,    0x40000, ""},
    /*
     * The Hobgoblin FPGA uses a Xilinx AXI UART 16550 v2.0, which is at
     * 0x60100000 and uses 8 KiB in the address space. However, the lower 4 KiB
     * do not contain any registers, they start at offset 4 KiB. To keep things
     * simple, we leave out the lower 4 KiB and just declare the upper 4 KiB
     * here. The acessible register are fully compatible with QEMU's existing
     * NS16550A UART emulation.
     */
    [HOBGOBLIN_UART0] =    { 0x60101000,     0x1000, ""},
    [HOBGOBLIN_SPI] =      { 0x60210000,     0x1000, ""},
    [HOBGOBLIN_GPIO0] =    { 0x60300000,    0x10000, ""},
    [HOBGOBLIN_GPIO1] =    { 0x60310000,    0x10000, ""},
    /* Each virtio transport channel uses 512 byte */
    [HOBGOBLIN_VIRTIO] =   { 0x70000000,    0x10000, ""},
    [HOBGOBLIN_DRAM] =     { 0x80000000, 0x40000000,
        "riscv.hobgoblin.ram"},
};

static const memmapEntry_t pro_fpga_memmap[] = {
    [0] =                  { 0x2000000000, 0x400000000,
        "riscv.hobgoblin.ram2"},
};

/* sifive_plic_create() parameters */
#define HOBGOBLIN_PLIC_NUM_SOURCES      32
#define HOBGOBLIN_PLIC_NUM_PRIORITIES   7
#define HOBGOBLIN_PLIC_PRIORITY_BASE    0x0000
#define HOBGOBLIN_PLIC_PENDING_BASE     0x1000
#define HOBGOBLIN_PLIC_ENABLE_BASE      0x2000
#define HOBGOBLIN_PLIC_ENABLE_STRIDE    0x80
#define HOBGOBLIN_PLIC_CONTEXT_BASE     0x200000
#define HOBGOBLIN_PLIC_CONTEXT_STRIDE   0x1000

/* CLINT timebase frequency */
#define CLINT_TIMEBASE_FREQ             100000000 /* 100 MHz */

/* Newer qemu versions define this in boot.h. */
#define RISCV64_BIOS_BIN "opensbi-riscv64-generic-fw_dynamic.bin"

static int hobgoblin_load_images(MachineState *machine, HobgoblinState_t *s)
{
    hwaddr start_addr;
    uint64_t kernel_entry = 0;
    uint64_t fdt_load_addr = 0;
    target_ulong firmware_end_addr;

    if (s->boot_from_rom) {
        /* Load the FSBL into ROM and set the ZSBL to point to it */
        start_addr = memmap[HOBGOBLIN_BOOT_ROM].base;
        firmware_end_addr = riscv_find_and_load_firmware(machine,
                                                         "fsbl_rom.xexe",
                                                         start_addr,
                                                         NULL);
    } else {
        target_ulong kernel_start_addr = 0;
        int fdt_size = 0;

        start_addr = memmap[HOBGOBLIN_DRAM].base;

        /* Read DTB */
        if (machine->dtb) {
            machine->fdt = load_device_tree(machine->dtb, &fdt_size);
            if (!machine->fdt) {
                error_report("load_device_tree() failed");
                exit(1);
            }
        }

        /* Load SBI into RAM */
        firmware_end_addr = riscv_find_and_load_firmware(machine,
                                                         RISCV64_BIOS_BIN,
                                                         start_addr,
                                                         NULL);

        /* Load Kernel into RAM */
        if (machine->kernel_filename) {
            kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc,
                                                             firmware_end_addr);
            kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                             kernel_start_addr, NULL);
        }

        /* Store (potentially modified) FDT into RAM */
        if (machine->fdt) {
            fdt_load_addr = riscv_load_fdt(memmap[HOBGOBLIN_DRAM].base,
                                           memmap[HOBGOBLIN_DRAM].size,
                                           machine->fdt);
        }
    }

    /*
     * If no kernel or FTD has been provided, kernel_entry and fdt_load_addr
     * can be 0 here. For QEMU, this is fine, as they are just parameters
     * passed to the bootloader, which has to cope with that.
     */
    riscv_setup_rom_reset_vec(machine, &s->soc, start_addr,
            memmap[HOBGOBLIN_MROM].base, memmap[HOBGOBLIN_MROM].size,
            kernel_entry, fdt_load_addr, machine->fdt);

    return 0;
}

static void hobgoblin_add_soc(HobgoblinState_t *s, MachineState *machine,
                              const int smp_cpus)
{
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_HART_ARRAY);

    object_property_set_str(OBJECT(&s->soc), "cpu-type",
                            TYPE_RISCV_CPU_CODASIP_A730, &error_abort);

    object_property_set_int(OBJECT(&s->soc), "num-harts",
                            smp_cpus, &error_abort);

    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);
}

static void hobgoblin_add_memory_area(MemoryRegion *system_memory,
                                      const memmapEntry_t *e)
{
    MemoryRegion *reg = g_new(MemoryRegion, 1);
    memory_region_init_ram(reg, NULL, e->name, e->size, &error_fatal);
    memory_region_add_subregion(system_memory, e->base, reg);
}

static void hobgoblin_add_interrupt_controller(HobgoblinState_t *s,
                                               const int num_harts)
{
    const memmapEntry_t *mem_plic = &memmap[HOBGOBLIN_PLIC];
    const memmapEntry_t *mem_clint = &memmap[HOBGOBLIN_CLINT];
    const int hartid_base = 0; /* Hart IDs start at 0 */

    /* PLIC */
    assert(HOBGOBLIN_PLIC_NUM_SOURCES >= HOBGOBLIN_MAX_IRQ);
    DeviceState *plic = sifive_plic_create(
        mem_plic->base,
        "MS", /* instances for both M-Mode and S-Mode */
        hartid_base,
        HOBGOBLIN_PLIC_NUM_SOURCES,
        HOBGOBLIN_PLIC_NUM_PRIORITIES,
        HOBGOBLIN_PLIC_PRIORITY_BASE,
        HOBGOBLIN_PLIC_PENDING_BASE,
        HOBGOBLIN_PLIC_ENABLE_BASE,
        HOBGOBLIN_PLIC_ENABLE_STRIDE,
        HOBGOBLIN_PLIC_CONTEXT_BASE,
        HOBGOBLIN_PLIC_CONTEXT_STRIDE,
        mem_plic->size);

    sifive_clint_create(mem_clint->base, mem_clint->size,
                        hartid_base, num_harts,
                        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE,
                        SIFIVE_TIME_BASE, CLINT_TIMEBASE_FREQ, true);

    /* publish */
    s->plic = plic;
}

static qemu_irq hobgoblin_make_plic_irq(HobgoblinState_t *s, int number)
{
    DeviceState *plic = s->plic;
    assert(plic); /* PLIC instance must exist. */
    return qdev_get_gpio_in(DEVICE(plic), number);
}

static void hobgoblin_connect_plic_irq(HobgoblinState_t *s,
        SysBusDevice *busDev, int dev_irq, int number)
{
    qemu_irq irq = hobgoblin_make_plic_irq(s, number);
    sysbus_connect_irq(busDev, dev_irq, irq);
}

static void hobgoblin_add_uart(HobgoblinState_t *s,
                               MemoryRegion *system_memory)
{
    const memmapEntry_t *mem_uart = &memmap[HOBGOBLIN_UART0];

    /* there must be an actual QEMU uart device */
    Chardev *chardev = serial_hd(0);
    assert(chardev);

    qemu_irq irq = hobgoblin_make_plic_irq(s, HOBGOBLIN_UART0_IRQ);

    serial_mm_init(system_memory, mem_uart->base, 2, irq, 115200,
                   chardev, DEVICE_LITTLE_ENDIAN);
}

static void hobgoblin_gpio_1_3_event(void *opaque, int n, int level)
{
    /* gpio pin active high triggers reset */
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void hobgoblin_add_gpio(HobgoblinState_t *s)
{
    for (int i = 0; i < 2; i++) {
        /* create GPIO */
        DeviceState *gpio = qdev_new(TYPE_XLNX_AXI_GPIO);
        SysBusDevice *bus_gpio = SYS_BUS_DEVICE(gpio);
        sysbus_realize_and_unref(bus_gpio, &error_fatal);
        sysbus_mmio_map(bus_gpio, 0, memmap[HOBGOBLIN_GPIO0 + i].base);
        /* connect PLIC interrupt */
        hobgoblin_connect_plic_irq(s, bus_gpio, 0, HOBGOBLIN_GPIO0_IRQ + i);
        /* publish GPIO device */
        s->gpio[i] = gpio;
    }

    /* Reset via GPIO 1.3 */
    qdev_connect_gpio_out(DEVICE(s->gpio[1]), 3,
                          qemu_allocate_irq(hobgoblin_gpio_1_3_event, NULL, 0));

}

static void hobgoblin_add_spi(HobgoblinState_t *s)
{
    const memmapEntry_t *mem_spi = &memmap[HOBGOBLIN_SPI];

    /* create SPI device */
    DeviceState *spi = qdev_new(TYPE_XILINX_SPI);
    SysBusDevice *bus_spi = SYS_BUS_DEVICE(spi);
    sysbus_realize_and_unref(bus_spi, &error_fatal);
    sysbus_mmio_map(bus_spi, 0, mem_spi->base);
    /* connect PLIC interrupt */
    hobgoblin_connect_plic_irq(s, bus_spi, 0, HOBGOBLIN_SPI_IRQ);

    /* create SD Card in SPI mode */
    DeviceState *sd_card_spi = qdev_new(TYPE_SD_CARD);
    DriveInfo *dinfo = drive_get(IF_SD, 0, 0);
    BlockBackend *blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    qdev_prop_set_drive_err(sd_card_spi, "drive", blk, &error_fatal);
    qdev_prop_set_bit(sd_card_spi, "spi", true);

    /* Connect SD card to SPI */
    SSIBus *bus_ssi = (SSIBus *)qdev_get_child_bus(spi, "spi");
    DeviceState *sd_dev = ssi_create_peripheral(bus_ssi, "ssi-sd");
    qdev_realize_and_unref(sd_card_spi,
                           qdev_get_child_bus(sd_dev, "sd-bus"),
                           &error_fatal);

    /* publish SPI device */
    s->spi = spi;
}

static void hobgoblin_add_ethernetlite(HobgoblinState_t *s)
{
    const memmapEntry_t *mem_eth = &memmap[HOBGOBLIN_ETHLITE];

    NICInfo *nd = &nd_table[0];
    const char *model = TYPE_XILINX_ETHLITE;

    /* Ethernet (ethernetlite) */
    qemu_check_nic_model(nd, model);
    DeviceState *eth = qdev_new(model);
    qdev_set_nic_properties(eth, nd);

    SysBusDevice *bus_eth = SYS_BUS_DEVICE(eth);
    sysbus_realize_and_unref(bus_eth, &error_fatal);
    sysbus_mmio_map(bus_eth, 0, mem_eth->base);
    /* connect PLIC interrupt */
    hobgoblin_connect_plic_irq(s, bus_eth, 0, HOBGOBLIN_ETH_IRQ);

    /* publish ETH device */
    s->eth = eth;
}

static void hobgoblin_add_axi_ethernet(HobgoblinState_t *s)
{
    const memmapEntry_t *mem_eth = &memmap[HOBGOBLIN_AXI_ETH];
    const memmapEntry_t *mem_dma = &memmap[HOBGOBLIN_AXI_DMA];
    NICInfo *nd = &nd_table[0];
    const char *eth_model = TYPE_XILINX_AXI_ETHERNET;

    qemu_check_nic_model(nd, eth_model);

    DeviceState *eth = qdev_new(eth_model);
    DeviceState *dma = qdev_new(TYPE_XILINX_AXI_DMA);

    /* FIXME: attach to the sysbus instead */
    object_property_add_child(qdev_get_machine(), "xilinx-eth", OBJECT(eth));
    object_property_add_child(qdev_get_machine(), "xilinx-dma", OBJECT(dma));

    Object *ds, *cs;
    ds = object_property_get_link(OBJECT(dma),
                                  "axistream-connected-target", NULL);
    cs = object_property_get_link(OBJECT(dma),
                                  "axistream-control-connected-target", NULL);
    assert(ds && cs);
    qdev_set_nic_properties(eth, nd);
    qdev_prop_set_uint32(eth, "phyaddr", 1);
    qdev_prop_set_uint32(eth, "rxmem", 0x4000);
    qdev_prop_set_uint32(eth, "txmem", 0x4000);
    object_property_set_link(OBJECT(eth), "axistream-connected", ds,
                             &error_abort);
    object_property_set_link(OBJECT(eth), "axistream-control-connected", cs,
                             &error_abort);

    SysBusDevice *eth_busdev = SYS_BUS_DEVICE(eth);
    sysbus_realize_and_unref(eth_busdev, &error_fatal);
    sysbus_mmio_map(eth_busdev, 0, mem_eth->base);
    hobgoblin_connect_plic_irq(s, eth_busdev, 0, HOBGOBLIN_ETH_IRQ);

    ds = object_property_get_link(OBJECT(eth),
                                  "axistream-connected-target", NULL);
    cs = object_property_get_link(OBJECT(eth),
                                  "axistream-control-connected-target", NULL);
    assert(ds && cs);
    qdev_prop_set_uint32(dma, "freqhz", 100 * 1000000);
    object_property_set_link(OBJECT(dma), "axistream-connected", ds,
                             &error_abort);
    object_property_set_link(OBJECT(dma), "axistream-control-connected", cs,
                             &error_abort);

    SysBusDevice *dma_busdev = SYS_BUS_DEVICE(dma);
    sysbus_realize_and_unref(dma_busdev, &error_fatal);
    sysbus_mmio_map(dma_busdev, 0, mem_dma->base);
    hobgoblin_connect_plic_irq(s, dma_busdev, 0, HOBGOBLIN_AXIDMA_IRQ0);
    hobgoblin_connect_plic_irq(s, dma_busdev, 1, HOBGOBLIN_AXIDMA_IRQ1);

    /* publish ETH device */
    s->eth = eth;
}

static void hobgoblin_add_virtio(HobgoblinState_t *s)
{
    const memmapEntry_t *mem_virtio = &memmap[HOBGOBLIN_VIRTIO];

    for (int i = 0; i < NUM_VIRTIO_TRANSPORTS; i++) {
        hwaddr offset = 0x200 * i;
        assert(offset < mem_virtio->size);
        hwaddr base = mem_virtio->base + offset;
        qemu_irq irq = hobgoblin_make_plic_irq(s, HOBGOBLIN_VIRTIO0_IRQ + i);
        sysbus_create_simple("virtio-mmio", base, irq);
    }
}

static void hobgoblin_machine_init(MachineState *machine)
{
    HobgoblinState_t *s = HOBGOBLIN_MACHINE_STATE(machine);

    MemoryRegion *system_memory = get_system_memory();

    /* Currently there is just one core. */
    const int smp_cpus = 1;

    hobgoblin_add_soc(s, machine, smp_cpus);

    /* add memory regions */
    hobgoblin_add_memory_area(system_memory, &memmap[HOBGOBLIN_DRAM]);
    hobgoblin_add_memory_area(system_memory, &memmap[HOBGOBLIN_MROM]);
    hobgoblin_add_memory_area(system_memory, &memmap[HOBGOBLIN_BOOT_ROM]);
    hobgoblin_add_memory_area(system_memory, &memmap[HOBGOBLIN_BOOT_RAM]);
    /* SRAM exists on FPGA only */
    hobgoblin_add_memory_area(system_memory, &memmap[HOBGOBLIN_SRAM]);

    if (s->board_type == BOARD_TYPE_PROFPGA) {
        hobgoblin_add_memory_area(system_memory, &pro_fpga_memmap[0]);
    }

    /* add interrupt controller */
    hobgoblin_add_interrupt_controller(s, smp_cpus);

    /* add peripherals (requires having an interrupt controller) */
    hobgoblin_add_uart(s, system_memory);
    hobgoblin_add_gpio(s);
    hobgoblin_add_spi(s);
    switch (s->eth_type) {
    case ETH_TYPE_ETHERNETLITE:
        hobgoblin_add_ethernetlite(s);
        break;
    case ETH_TYPE_AXI_ETHERNET:
        hobgoblin_add_axi_ethernet(s);
        break;
    }
    hobgoblin_add_virtio(s);

    /* load images into memory to boot the platform */
    int ret = hobgoblin_load_images(machine, s);
    if (ret != 0) {
        error_report("loading images failed (%d)", ret);
        exit(1);
    }
}

static bool hobgoblin_machine_get_boot_from_rom(Object *obj, Error **errp)
{
    HobgoblinState_t *s = HOBGOBLIN_MACHINE_STATE(obj);

    return s->boot_from_rom;
}

static void hobgoblin_machine_set_boot_from_rom(Object *obj, bool value,
                                                Error **errp)
{
    HobgoblinState_t *s = HOBGOBLIN_MACHINE_STATE(obj);

    s->boot_from_rom = value;
}

static char *hobgoblin_machine_get_board_type(Object *obj, Error **errp)
{
    HobgoblinState_t *s = HOBGOBLIN_MACHINE_STATE(obj);
    const char *result;

    switch (s->board_type) {
    case BOARD_TYPE_GENESYS2:
        result = "genesys2";
        break;
    case BOARD_TYPE_PROFPGA:
        result = "profpga";
        break;
    default:
        result = "Unknown";
        break;
    }

    return (char*)result;
}

static void hobgoblin_machine_set_board_type(Object *obj, const char *value,
                                             Error **errp)
{
    HobgoblinState_t *s = HOBGOBLIN_MACHINE_STATE(obj);

    if (!strcmp(value, "genesys2"))
        s->board_type = BOARD_TYPE_GENESYS2;
    else if (!strcmp(value, "profpga"))
        s->board_type = BOARD_TYPE_PROFPGA;
    else
        error_setg(errp, "Unrecognised board-type");
}

static char *hobgoblin_machine_get_eth_type(Object *obj, Error **errp)
{
    HobgoblinState_t *s = HOBGOBLIN_MACHINE_STATE(obj);
    const char *result;

    switch (s->eth_type) {
    case ETH_TYPE_AXI_ETHERNET:
        result = "axi-ethernet";
        break;
    case ETH_TYPE_ETHERNETLITE:
        result = "ethernetlite";
        break;
    default:
        result = "Unknown";
        break;
    }

    return (char*)result;
}

static void hobgoblin_machine_set_eth_type(Object *obj, const char *value,
                                           Error **errp)
{
    HobgoblinState_t *s = HOBGOBLIN_MACHINE_STATE(obj);

    if (!strcmp(value, "axi-ethernet"))
        s->eth_type = ETH_TYPE_AXI_ETHERNET;
    else if (!strcmp(value, "ethernetlite"))
        s->eth_type = ETH_TYPE_ETHERNETLITE;
    else
        error_setg(errp, "Unrecognised eth-type");
}

static void hobgoblin_machine_instance_init(Object *obj)
{
    HobgoblinState_t *s = HOBGOBLIN_MACHINE_STATE(obj);

    s->boot_from_rom = false;
    s->board_type = BOARD_TYPE_GENESYS2;
    s->eth_type = ETH_TYPE_AXI_ETHERNET;
}

static void hobgoblin_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = hobgoblin_machine_init;
    /* mc->reset:   void reset(MachineState *state, ShutdownCause reason); */
    /* mc->wakeup:  void wakeup(MachineState *state); */
    mc->max_cpus = 1;

    object_class_property_add_bool(oc, "boot-from-rom",
                                   hobgoblin_machine_get_boot_from_rom,
                                   hobgoblin_machine_set_boot_from_rom);
    object_class_property_set_description(oc, "boot-from-rom",
        "Load BIOS (default fsbl_rom.xexe) into ROM and boot into it");

    object_class_property_add_str(oc, "board-type",
                                  hobgoblin_machine_get_board_type,
                                  hobgoblin_machine_set_board_type);
    object_class_property_set_description(oc, "board-type",
        "Set the board type (genesys2 (default) or profpga)");

    object_class_property_add_str(oc, "eth-type",
                                  hobgoblin_machine_get_eth_type,
                                  hobgoblin_machine_set_eth_type);
    object_class_property_set_description(oc, "eth-type",
        "Set the Ethernet type (axi-ethernet (default) or ethernetlite)");
}

static const TypeInfo hobgoblin_typeinfo = {
    .name       = MACHINE_TYPE_NAME("hobgoblin"),
    .parent     = TYPE_MACHINE,
    .class_init = hobgoblin_machine_class_init,
    .instance_size = sizeof(HobgoblinState_t),
    .instance_init = hobgoblin_machine_instance_init,
    .class_init    = hobgoblin_machine_class_init,
};

static void hobgoblin_register_types(void)
{
    type_register_static(&hobgoblin_typeinfo);
}

type_init(hobgoblin_register_types)

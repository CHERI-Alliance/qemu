
# QEMU-CHERI

This repository contains a version of QEMU with CHERI support. It is based on
upstream QEMU 7.0 and the [QEMU CHERI implementation from Cambridge
University](https://github.com/CTSRD-CHERI/qemu). and the [CHERI Alliance](https://github.com/CHERI-Alliance/qemu)

QEMU-CHERI supports system emulation of RISC-V 32 and 64-bit machines. It
implements the [RISC-V CHERI specification
v0.9.3](https://github.com/riscv/riscv-cheri/releases/tag/v0.9.3-prerelease).
As well as the CHERI v9 specification for both RISC-V and Arm Morello, and MIPS.

## Building

Building the CHERI version of QEMU is not different from the usual build
process. There are several new targets
- riscv64xcheri-softmmu
- riscv64cheristd-softmmu
- riscv32xcheri-softmmu
- riscv32cheristd-softmmu
- morello-softmmu
- mips64cheri128-softmmu

The MIPS and Morello targets are obvious. For RISC-V the `xcheri` targets are the
CHERI v9 targets and the `cheristd` targets are the RISC-V CHERI standard v0.93
currently going through ratification with RISC-V.

QEMU-CHERI emulates a minimum system, a lot of QEMU's additional features can
be disabled.

```
$ mkdir build
$ cd build
$ ../configure --target-list="riscv32cheristd-softmmu riscv64cheristd-softmmu" \
   --disable-gtk --audio-drv-list="" --disable-brlapi --disable-libiscsi \
   --disable-libnfs --disable-rbd --disable-sdl --disable-snappy \
   --disable-vnc --disable-vnc-jpeg --disable-vnc-sasl --disable-l2tpv3 \
   --disable-oss --disable-alsa --disable-tpm --disable-werror --meson=git
$ ninja
```

## Running guest software

CHERI RISC-V system emulation works with the generic virt machine.
Running a standalone application is as simple as

```
$ qemu-system-riscv64cheristd -M virt -nographic -semihosting -bios ./hello
Hello world!
$
```

QEMU-CHERI has support for the Codasip Prime FPGA  platform. `-M codasip-prime`
can be set to select this platform. This will configure the devices and CPU
with the equivalent to the Codasip-prime platform.

It's possible to see the executed assembler instruction and register updates
by adding `-d instr` to QEMU's commandline.

### CPU properties

QEMU-CHERI defines some additional CPU properties to configure the CHERI

#### For the riscv..cheristd targets

* `y`

Configures the y extension (CHERI)

* `Zyhybrid`

Controls the cheri hybrid support

* `Zylevels1`

Controls cheri levels support (replaces cheri_levels)

* `Svucrg`

Controls the CHERI PTE CRG support (replaces cheri_pte)
* `zish4add`

Enable the zish4add extension support.

#### For the riscv..cheriX targets

* `Xcheri`

Enable CHERI Support

* Xcheri_v9

Use CHERI V9 ISA



## Limitations


QEMU's userspace emulation does not support CHERI yet.

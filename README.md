
# QEMU-CHERI

This repository contains a version of QEMU with CHERI support. It is based on
upstream QEMU 6.2 and the [QEMU CHERI implementation from Cambridge
University](https://github.com/CTSRD-CHERI/qemu).

QEMU-CHERI supports system emulation of RISC-V 32 and 64-bit machines. It
implements the [RISC-V CHERI specification
v0.8.3](https://github.com/riscv/riscv-cheri/releases/tag/v0.8.3-prerelease).

## Building

Building the CHERI version of QEMU is not different from the usual build
process. There's two new targets `riscv32cheri-softmmu` and
`riscv64cheri-softmmu`.

QEMU-CHERI emulates a minimum system, a lot of QEMU's additional features can
be disabled.

```
$ mkdir build
$ cd build
$ ../configure --target-list="riscv32cheri-softmmu riscv64cheri-softmmu" \
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
$ qemu-system-riscv64cheri -M virt -nographic -semihosting -bios ./hello
Hello world!
$
```

QEMU-CHERI has support for Codasip's hobgoblin platform. `-M
hobgoblin-genesys2` or `-M hobgoblin-profpga` can be set to select the two
hardware versions of this platform.

It's possible to see the executed assembler instruction and register updates
by adding `-d instr` to QEMU's commandline.

### CPU properties

QEMU-CHERI defines some additional CPU properties to configure the CHERI
implementation.

* `Xcheri_purecap`

This is a boolean property. If enabled, QEMU-CHERI runs in purecap mode. By
default, `Xcheri_purecap` is off and QEMU-CHERI runs in hybrid mode.

* `cheri_v090`

This is a boolean property. If enabled, QEMU-CHERI runs in a mode compatible
with the v0.9 version of the CHERI specificaiton. This flag defaults to off
at present. It includes setting the v0.9 orientation of the m_flip flag as well
as updated permissions definition and new instructions.

* `cheri_pte`

This is a boolean property which defaults to off. If enabled it will activate
the ZCheriPTE part of the the Cheri specification. Enabling this extension
requires PTE entries to include PTE_CW and PTE_CRG configuration, if these are
not present then attempts to read/write capabilities to virtual addresses will
generate a page fault.

* `m_flip`

This boolean property also defaults to off. It can be enabled for guest
software that has not yet been updated to RISC-V CHERI v0.8.3 and needs the
legacy definition of a capability's M bit. This flag should be deprecated.

Here's an example for setting the properties.

```
$ qemu-system-riscv64cheri \
    -cpu codasip-a730,Xcheri_purecap=on,cheri_v090=on,cheri_pte=on \
    -M virt -nographic -semihosting -bios ./hello
```

## Limitations

The code contains CHERI implementations for mips and morello platforms that
have not been tested.

QEMU's userspace emulation does not support CHERI yet.

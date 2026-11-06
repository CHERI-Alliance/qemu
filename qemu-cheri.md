# QEMU-CHERI

This repository contains a version of QEMU with CHERI support. It is based on
upstream QEMU 8.1, the [QEMU CHERI implementation from Cambridge
University](https://github.com/CTSRD-CHERI/qemu) and the
[CHERI Alliance](https://github.com/CHERI-Alliance/qemu) fork.

QEMU-CHERI supports system emulation of RISC-V 32 and 64-bit machines. It
implements the [RISC-V CHERI specification
v0.9.3](https://github.com/riscv/riscv-cheri/releases/tag/v0.9.3-prerelease),
as well as the CHERI v9 specification for RISC-V, Arm Morello and MIPS.

This guide only describes the differences from regular QEMU and should be used
together with the regular QEMU documentation, see [README.rst](README.rst).

## Building

Building the CHERI version of QEMU is no different from the usual build
process. There are several new targets:

- `riscv64cheristd-softmmu`
- `riscv32cheristd-softmmu`
- `riscv64xcheri-softmmu`
- `riscv32xcheri-softmmu`
- `morello-softmmu`
- `mips64cheri128-softmmu`

The MIPS and Morello targets are self-explanatory. For RISC-V, the `cheristd`
targets implement the RISC-V CHERI standard v0.9.3, which is currently going
through ratification with RISC-V International, and the `xcheri` targets
implement CHERI v9.

QEMU-CHERI emulates a minimal system, so many of QEMU's additional features can
be disabled:

```
$ ./configure --target-list=riscv32cheristd-softmmu,riscv64cheristd-softmmu \
   --disable-docs --disable-gtk --disable-brlapi --disable-libiscsi \
   --disable-libnfs --disable-rbd --disable-sdl --disable-snappy
$ cd build
$ ninja
```

## Running guest software

CHERI RISC-V system emulation works with the generic `virt` machine.
Running a standalone application is as simple as:

```
$ qemu-system-riscv64cheristd -M virt -nographic -semihosting -bios ./hello
Hello world!
```

### Additional machines

In addition to `virt`, the RISC-V targets support the following Codasip FPGA
platforms, selected with `-M <name>`:

- `codasip-prime` - the Codasip Prime FPGA platform (see
  [codasip-prime.md](codasip-prime.md))
- `hobgoblin-v2-vcu118`, `hobgoblin-v2-profpga`, `hobgoblin-v2-genesys2`
- `hobgoblin-vcu118`, `hobgoblin-profpga`, `hobgoblin-genesys2`

Use the machine that matches your board. These machines generate a device tree
describing the emulated platform; to use a different one, pass it with `-dtb`.

### CPU properties

QEMU-CHERI defines some additional CPU properties to configure the CHERI
implementation.

#### For the `riscv*cheristd` targets

* `y` - enables the Y (CHERI) extension.
* `Zyhybrid` - enables CHERI hybrid mode support.
* `Zylevels1` - enables CHERI levels support (replaces `cheri_levels`).
* `Svucrg` - enables the CHERI PTE CRG support (replaces `cheri_pte`).
* `zish4add` - enables the Zish4add extension.

#### For the `riscv*xcheri` targets

* `Xcheri` - enables CHERI support.
* `Xcheri_v9` - uses the CHERI v9 ISA.

These properties can be set on the generic `rv64`/`rv32` CPUs, for example:

```
$ qemu-system-riscv64cheristd -M virt -nographic -semihosting \
    -cpu rv64,y=on,Zyhybrid=on,Zylevels1=on,Svucrg=on -bios ./hello
```

The named Codasip CPUs (for example `codasip-x730`) have a fixed configuration
and do not accept extension properties. To start from a Codasip CPU's
configuration and change it, use the equivalent generic CPU strings described
in [codasip-prime.md](codasip-prime.md#cpu-configurations).

## Logging

The executed instructions and register updates can be logged by adding
`-d instr` to QEMU's command line.

## Limitations

QEMU's userspace (linux-user) emulation does not support CHERI; only system
emulation does.

## Other points of note

QEMU-CHERI supports the GDB server protocol, so a CHERI-enabled version of GDB
can connect as described in the QEMU documentation.

The RISC-V CHERI targets can record where each capability lost its tag; see
[README.CHERI-tag-tracing.md](README.CHERI-tag-tracing.md).

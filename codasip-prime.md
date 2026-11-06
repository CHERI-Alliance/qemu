# Codasip Prime

This guide is in three parts. The Quickstart gets Codasip Prime users up and
running as quickly as possible. CPU configurations lists the Codasip CPUs QEMU
models and how to customise them. The Background gives some context on QEMU and
what to expect of the system.

See also [qemu-cheri.md](qemu-cheri.md), which describes the CHERI-specific
command line parameters, and [README.rst](README.rst) for general QEMU
documentation.

## Quickstart

Minimal installation instructions, assuming Rocky 9 or a similar distribution.

### Prerequisites

```bash
# This list is the minimum for a Rocky 9 based system. Package names and
# install methods may differ on other distributions; see the full QEMU
# documentation for more information.
dnf -y install dnf-plugin-config-manager
dnf -y config-manager --set-enabled crb
dnf -y install autoconf automake python3 libmpc-devel mpfr-devel gmp-devel \
        gawk bison flex patchutils gcc gcc-c++ zlib-devel expat-devel \
        diffutils file findutils git make which \
        pixman-devel acpica-tools mtools \
        expect \
        glib2-static \
        dosfstools \
        bzip2 texinfo python3-sphinx_rtd_theme python3-sphinx ninja-build \
        python3-pip
```

### Configure and build

The instructions below assume the repository is cloned (or the release package
extracted) into `~/qemu-cheri`; adjust the paths if not.

```bash
QEMU_DIR=~/qemu-cheri
cd $QEMU_DIR
# Disable unused features to speed up the build.
./configure --target-list=riscv64cheristd-softmmu \
    --disable-docs --disable-gtk --disable-brlapi --disable-libiscsi \
    --disable-libnfs --disable-rbd --disable-sdl --disable-snappy \
    --disable-vnc --disable-werror
cd $QEMU_DIR/build
ninja
# No install step is needed; skipping it avoids conflicts with any QEMU
# already installed on the system.
```

### Running

The `codasip-prime` machine models the Codasip Prime FPGA platform, including
its default `codasip-x730` CPU (see [CPU configurations](#cpu-configurations)).

#### Direct binary execution

For self-contained binaries, such as those produced by the bare-metal SDK or
the hello world test packaged with the release:

```bash
$QEMU_DIR/build/qemu-system-riscv64cheristd -M codasip-prime -nographic \
    -bios $sdkpath/hello_hobgoblinv2_hybrid.elf
```

#### Linux

Booting Linux needs several files from the SDK release, plus access to storage
and the network. The example below uses virtio for the disk and network, and
forwards host port 5555 to the guest's SSH port so that `ssh -p 5555
root@127.0.0.1` reaches the guest.

```bash
$QEMU_DIR/build/qemu-system-riscv64cheristd -M codasip-prime -m 1G \
    -bios $sdkpath/fw_dynamic.bin -kernel $sdkpath/Image \
    -drive file=$sdkpath/core-image-minimal-hobgoblin-vcu118.sdcard.wic,id=virtio0,format=raw \
    -device virtio-blk-device,drive=virtio0 \
    -append "root=/dev/vda2 console=ttyS0" \
    -device virtio-net-device,bus=virtio-mmio-bus.0,netdev=net0 \
    -netdev user,id=net0,hostfwd=tcp::5555-:22 \
    -nographic
```

The machine generates a device tree describing the emulated platform. To use a
different one, pass it with `-dtb`.

### Limitations and caveats

Only full system emulation is supported; there is no QEMU user mode support.

QEMU is an instruction-accurate emulator. It does not model timing, so no
performance conclusions can be drawn from it.

## CPU configurations

QEMU models the following Codasip CPUs as named CPUs, selected with
`-cpu <name>`:

| CPU                  | XLEN | CHERI | Target(s)                    |
|----------------------|------|-------|------------------------------|
| `codasip-x730`, `codasip-x730-lux` | 64 | yes | `riscv64cheristd` |
| `codasip-y730-quartz` | 64  | yes   | `riscv64cheristd`            |
| `codasip-1110-apex`  | 64   | yes   | `riscv64cheristd`            |
| `codasip-a730-flint` | 64   | no    | all 64-bit RISC-V targets    |
| `codasip-h730-garnet` | 64  | no    | all 64-bit RISC-V targets    |
| `codasip-1110-aria`  | 64   | no    | all 64-bit RISC-V targets    |
| `codasip-1110-aster` | 64   | no    | all 64-bit RISC-V targets    |
| `codasip-v739`, `codasip-v739-spinel` | 32 | yes | `riscv32cheristd` |
| `codasip-v730-shine` | 32   | no    | `riscv32cheristd`            |
| `codasip-l730-glim`  | 32   | no    | all 32-bit RISC-V targets    |
| `codasip-l739-topaz` | 32   | no    | all 32-bit RISC-V targets    |

The Codasip FPGA machines (`codasip-prime` and the `hobgoblin-*` machines)
default to `codasip-x730` on `riscv64cheristd`, `codasip-v739-spinel` on
`riscv32cheristd`, `codasip-a730-flint` on non-CHERI 64-bit targets and
`codasip-l730-glim` on non-CHERI 32-bit targets. Use `-cpu` to select a
different one.

### Customising a CPU configuration

The named CPUs have a fixed configuration and do not accept extension
properties, so a command line such as `-cpu codasip-x730,x-zicond=on` is
rejected. To experiment with a modified configuration, start from the generic
`rv64`/`rv32` CPU configured to match the named CPU and append the properties
to change.

[`scripts/cpu_configs_env`](scripts/cpu_configs_env) provides these strings.
Source it in a shell to define one environment variable per named CPU:

| Variable              | Equivalent named CPU |
|-----------------------|----------------------|
| `CODASIP_X730`, `CODASIP_X730_LUX` | `codasip-x730`, `codasip-x730-lux` |
| `CODASIP_Y730_QUARTZ` | `codasip-y730-quartz` |
| `CODASIP_1110_APEX`   | `codasip-1110-apex`  |
| `CODASIP_A730_FLINT`  | `codasip-a730-flint` |
| `CODASIP_H730_GARNET` | `codasip-h730-garnet` |
| `CODASIP_1110_ARIA`   | `codasip-1110-aria`  |
| `CODASIP_1110_ASTER`  | `codasip-1110-aster` |
| `CODASIP_V739_SPINEL`, `CODASIP_V739` | `codasip-v739-spinel`, `codasip-v739` |
| `CODASIP_V730_SHINE`  | `codasip-v730-shine` |
| `CODASIP_L730_GLIM`   | `codasip-l730-glim`  |
| `CODASIP_L739_TOPAZ`  | `codasip-l739-topaz` |

For example, to run the Prime platform with an X730 that also has the Zicond
extension:

```bash
source $QEMU_DIR/scripts/cpu_configs_env
$QEMU_DIR/build/qemu-system-riscv64cheristd -M codasip-prime -nographic \
    -cpu "$CODASIP_X730,x-zicond=on" -bios ./hello.elf
```

Each string enables the same ISA extensions, MMU type, PMP, PMU counter count
and privileged spec version as the named CPU. Things to be aware of:

- The generic CPUs do not implement the Codasip custom CSRs. Software that
  accesses them needs the named CPU.
- The strings are written for the `riscv64cheristd`/`riscv32cheristd` targets.
  The non-CHERI configurations turn the CHERI extensions (`y`, `Zyhybrid`,
  `zish4add`) off explicitly, because the generic CPUs enable them by default
  on those targets.
- Properties appended later on the command line override earlier ones, so a
  property in the base string can also be turned off, e.g.
  `"$CODASIP_X730,svpbmt=off"`.

## Background

Codasip Prime is Codasip's CHERI exploration kit, consisting of an FPGA
platform and an SDK. The SDK includes a toolchain, the QEMU emulation
environment and operating systems.

The FPGA platform is configured with a Codasip X730-MP4 CPU with CHERI support
based on version 0.9.3 of the RISC-V CHERI specification.

The CHERI-enabled version of QEMU is based on QEMU 8.1. It was initially
developed by the University of Cambridge and other CHERI contributors to
support CHERI research and development on Arm, MIPS and RISC-V. Codasip has
updated it to match the RISC-V CHERI specification currently going through
ratification; the version currently supported is 0.9.3.

The repository is publicly available in the CHERI Alliance GitHub organisation
and is maintained by members of the CHERI Alliance, including Codasip.

QEMU is a virtualisation and emulation environment capable of full system
emulation for many CPU architectures. When the guest architecture differs from
the host, QEMU performs dynamic binary translation, converting guest code to
host code as it runs. This gives a flexible and fast emulation environment that
can run a complete operating system or simpler bare-metal code. QEMU's
popularity means many emulated devices are available, so complete systems can
be emulated quite closely.

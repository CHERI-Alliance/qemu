Tracing where CHERI capability tags are cleared
===============================================

A CHERI tag violation tells you *that* a capability was untagged when it was
used, but not *where* it lost its tag. The tag may have been cleared many
instructions earlier and the untagged value then copied between registers or
spilled to memory and reloaded. Tag tracing records, for every untagged
capability, the guest PC of the instruction that cleared its tag and the reason
why. The record travels with the value through register copies and through
memory, and it is reported when a tag violation is raised on it.

Tag tracing is currently supported on the RISC-V CHERI targets
(`qemu-system-riscv64cheristd`, `qemu-system-riscv32cheristd`).

Building
--------

Tag tracing is compiled in only when QEMU is configured with
`--enable-tag-trace`:

```
./configure --target-list=riscv64cheristd-softmmu --enable-tag-trace ...
```

The configure summary shows `CHERI Tag tracing : YES`. Without this option
none of the commands or options below exist, and there is no run-time cost.

Turning it on
-------------

Even when compiled in, tracing is **off** by default, because collecting it
slows the guest down (in one measurement, a Linux boot took about 30% longer
with it on). There are three ways to switch it on, and they all control the
same switch:

1. **From startup**, with the `-cheri-tag-trace` command-line option.
2. **From the QEMU monitor**, with `cheri_tag_trace on` / `cheri_tag_trace off`
   (see [Using the monitor](#using-the-monitor) below).
3. **From guest code**, with these instructions, which execute as no-ops when
   tracing is not compiled in:

   | Instruction             | Effect             |
   |-------------------------|--------------------|
   | `slti zero, zero, 0x3b` | Start tag tracing  |
   | `slti zero, zero, 0x3e` | Stop tag tracing   |

   This lets a test or a piece of software bracket just the region it cares
   about, for example from C:

   ```c
   #define TAG_TRACE_START() __asm__ volatile("slti zero, zero, 0x3b")
   #define TAG_TRACE_STOP()  __asm__ volatile("slti zero, zero, 0x3e")
   ```

   These are separate from the `slti zero, zero, 0x1b`/`0x1e` instructions that
   start and stop instruction tracing (`-d instr`), so the two can be used
   independently.

Starting tracing (by any of these means) discards everything recorded before:
while tracing was off, registers and memory changed without being tracked, so
older records could describe the wrong value. Only tag clears that happen while
tracing is on are ever reported.

Reading the output
------------------

Recorded information is printed when a CHERI tag-violation exception is raised,
as part of the interrupt log, so run QEMU with `-d int` (add `-D <file>` to send
the log to a file). The line appears just before QEMU's own report of the
exception:

```
Tag trace: register <n> untagged (<cause>) at pc <pc>
```

where `<n>` is the capability register the exception names, `<pc>` is the guest
PC of the instruction that cleared the tag, and `<cause>` is one of:

| Cause                                         | Meaning |
|-----------------------------------------------|---------|
| `unrepresentable address`                     | The address was moved outside the representable range (e.g. `cadd`, `scaddr`). |
| `invalid bounds`                              | Bounds could not be set as requested (e.g. `scbnds` with an inexact length). |
| `insufficient permissions`                    | An operation required a permission the capability lacked. |
| `sealed capability modified`                  | A sealed capability was modified or used in a way that clears its tag. |
| `untagged authorising capability`             | The result was derived from an untagged capability. |
| `invalid seal`                                | Sealing or unsealing failed its checks. |
| `explicit tag clear`                          | The tag was cleared deliberately. |
| `stored without capability permission`        | Stored through an authorising capability without the C permission. |
| `local capability stored without permission`  | A non-global capability was stored without the SL permission. |
| `loaded without capability permission`        | Loaded through an authorising capability without the C permission. |
| `loaded from page without capability read`    | Loaded from a page whose page-table entry does not allow capability reads (`CW` clear). |
| `loaded from memory without tag storage`      | Loaded from memory that cannot hold tags (ROM, MMIO); any tag was lost when the value was stored there. The PC is that of the load. |
| `integer write`                               | The register was last written with an integer value; there is no clearing PC. |
| `no recorded clear`                           | The value was never tagged while tracing was on (e.g. it was loaded from ordinary data), or was recorded before tracing was last started. |

When no PC is known the line ends in `clearing pc unknown` instead of
`at pc <pc>`.

Example: a tag cleared, copied, spilled and reloaded
----------------------------------------------------

This bare-metal RISC-V fragment (running in capability mode) clears a tag by
moving a bounded capability out of its representable range, copies the result
to another register, stores it to memory, clobbers both registers, reloads it,
and only then uses it:

```
80000030:  cadd   cgp, cra, t0      # t0 = 1 << 62: result is unrepresentable,
                                    # so its tag is cleared HERE
80000034:  cmv    ctp, cgp          # copy to another register
80000038:  sc     ctp, 0x0(csp)     # spill to memory
80000044:  cmv    cgp, cnull        # clobber both registers
80000048:  cmv    ctp, cnull
8000004c:  lc     ctp, 0x0(csp)     # reload
80000050:  lc     ct0, 0x0(ctp)     # use it: tag violation on ctp (c4)
```

Without tag tracing, the log only says where the capability was *used*:

```
$ qemu-system-riscv64cheristd -M virt -cpu codasip-x730 -bios none \
      -kernel test.elf -nographic -semihosting -d int
Got CHERI trap Tag Violation, caused by register 4
riscv_cpu_do_interrupt: hart:0, async:0, cause:000000000000001c, epc:0x0000000080000050, tval:0x4000000080000014, desc=cheri_fault
```

With tag tracing on, it also says where the tag was *lost*, even though the
value went through another register and through memory in between:

```
$ qemu-system-riscv64cheristd -M virt -cpu codasip-x730 -bios none \
      -kernel test.elf -nographic -semihosting -d int -cheri-tag-trace
Tag trace: register 4 untagged (unrepresentable address) at pc 0000000080000030
Got CHERI trap Tag Violation, caused by register 4
riscv_cpu_do_interrupt: hart:0, async:0, cause:000000000000001c, epc:0x0000000080000050, tval:0x4000000080000014, desc=cheri_fault
```

Other situations are reported the same way. For example, a capability stored
through an authorising capability that lacks the C permission, and later loaded
back through one that has it:

```
Tag trace: register 4 untagged (stored without capability permission) at pc 0000000080000040
```

or a capability loaded in S-mode through a mapping whose page-table entry has
`CW` clear:

```
Tag trace: register 4 untagged (loaded from page without capability read) at pc 00000000800000b4
```

In each case the PC is that of the `sc` or `lc` that lost the tag, not of the
later instruction that faulted.

Using the monitor
-----------------

The QEMU monitor lets you switch tag tracing on or off while the machine is
running, or before it starts, without changing the guest or the command line.

The `cheri_tag_trace` monitor command takes an optional argument:

- `cheri_tag_trace on` starts tracing (discarding anything recorded before);
- `cheri_tag_trace off` stops it;
- `cheri_tag_trace` on its own shows whether tracing is on, and how many
  distinct clear sites have been stored to memory so far.

Switching takes effect as soon as every vCPU has finished the block of code it
is running (immediately if the machine is paused). If you query the state in
between, it shows the switch as pending, e.g. `CHERI tag trace: off (switching
on)`.

There are several ways to reach the monitor:

- **On the terminal, alongside the serial console:** use `-serial mon:stdio`
  (this is what `-nographic` does by default). Press `Ctrl-a c` to switch
  between the guest console and the `(qemu)` prompt.
- **On the terminal, instead of the serial console:** use `-monitor stdio`
  (and send the serial port elsewhere, e.g. `-serial file:serial.log`).
- **From another terminal or a script:** give the monitor a Unix socket, e.g.
  `-monitor unix:/tmp/qemu-mon.sock,server,nowait`, then connect to it with
  `socat - unix-connect:/tmp/qemu-mon.sock` (or `nc -U /tmp/qemu-mon.sock`).
  Keep the socket path short: Unix socket paths are limited to 107 characters.

A typical session enables tracing before the guest runs any code: start QEMU
paused with `-S`, attach the monitor, switch tracing on and continue:

```
$ qemu-system-riscv64cheristd -M virt -cpu codasip-x730 -bios none \
      -kernel test.elf -semihosting -display none \
      -serial file:serial.log -d int -D qemu.log \
      -monitor unix:/tmp/qemu-mon.sock,server,nowait -S &

$ socat - unix-connect:/tmp/qemu-mon.sock
QEMU 8.1.0 monitor - type 'help' for more information
(qemu) cheri_tag_trace
CHERI tag trace: off
  clear sites recorded: 0
(qemu) cheri_tag_trace on
(qemu) cheri_tag_trace
CHERI tag trace: on
  clear sites recorded: 0
(qemu) cont
```

After the guest hits the tag violation, `qemu.log` contains:

```
Tag trace: register 4 untagged (unrepresentable address) at pc 0000000080000030
Got CHERI trap Tag Violation, caused by register 4
riscv_cpu_do_interrupt: hart:0, async:0, cause:000000000000001c, epc:0x0000000080000050, tval:0x4000000080000014, desc=cheri_fault
```

The same commands work on a running machine. For example, while booting
Linux you can leave tracing off through the boot, then switch it on just
before running the program you are debugging:

```
(qemu) cheri_tag_trace on
(qemu) cheri_tag_trace
CHERI tag trace: on
  clear sites recorded: 2
```

Limitations
-----------

- Only RISC-V CHERI targets report tag clears. Morello and CHERI-MIPS build
  with `--enable-tag-trace` but do not report, and do not carry records through
  all their load paths.
- Records are only printed at tag-violation exceptions, under `-d int`. There
  is no command yet to query the record of an arbitrary register or address.
- A tag lost by storing to memory that cannot hold tags is reported at the
  later load, not at the store.
- Some causes group several related checks; for example `insufficient
  permissions` does not say which permission was missing.
- With several vCPUs, a store and a capability store racing on the same
  location can leave a record for the other value. Treat reports as a
  debugging aid, not a guarantee.

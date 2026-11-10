# Bare-metal CHERI regression tests

> Note: This test suite and all tests in `tests/cheri/` are AI-generated,
> prompted to generate adversarial regression tests that expose MTTCG
> capability tag memory bugs.

Small freestanding C tests for `qemu-system-riscv64y` and
`qemu-system-morello`, built with a stock clang and ld.lld (skipped if none is
found). Run them with:

    meson test -C build --suite cheri --print-errorlogs

To add a test, write `<arch>-<name>.c` (start with a comment explaining what it
checks, `#include "rvy.h"` or `"morello.h"`, define `int test_main(void)`
returning `0` on success or calling `check(cond, code)` / `exit_test(code)` on
failure, and optionally define `void secondary_main(unsigned long cpu_id)` for
SMP tests) and add an entry to `cheri_tests` in `meson.build`. CHERI
instructions that stock clang does not know are wrapped in inline assembly
helpers in `include/rvy.h` and `include/morello.h`.

Tests with a `'gdb'` script run QEMU under `gdb -batch -x <script>`, and
the script must `run` the guest and `quit` with the verdict. They are
skipped (exit 77) unless gdb is installed and QEMU was configured with
`--enable-debug` (debug info and `-O0`).

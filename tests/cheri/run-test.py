#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Note: This test runner is fully AI-generated (prompted to generate
# adversarial regression tests exposing MTTCG capability tag memory bugs).
"""Run one bare-metal CHERI test and exit with the guest's semihosting
exit status (or gdb's exit status for --gdb tests). Exits 77 (meson skip)
for --gdb tests unless gdb is installed and QEMU was built with -g -O0.
With --log, QEMU runs with -d instr and every REGEX must match the log.

Usage: run-test.py [--gdb SCRIPT [--debug-build]] [--log REGEX [--log-instr]]
                   [--timeout S] QEMU ARGS...
"""

import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile

SKIP = 77


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--gdb", metavar="SCRIPT",
                    help="run QEMU under gdb -batch -x SCRIPT")
    ap.add_argument("--debug-build", action="store_true",
                    help="QEMU was built with debug info and -O0")
    ap.add_argument("--log", metavar="REGEX", action="append", default=[],
                    help="require REGEX in the -d instr log (repeatable)")
    ap.add_argument("--log-instr", action="store_true",
                    help="QEMU was built with --enable-tcg-log-instr")
    ap.add_argument("--timeout", type=float, default=45)
    ap.add_argument("qemu", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    cmd = args.qemu
    env = None
    if args.log:
        if not args.log_instr:
            print("SKIP: --log tests need --enable-tcg-log-instr")
            return SKIP
        logdir = tempfile.TemporaryDirectory()
        logfile = os.path.join(logdir.name, "instr.log")
        cmd = cmd + ["-d", "instr", "-D", logfile]
    if args.gdb:
        if not args.debug_build:
            print("SKIP: gdb tests need QEMU configured with --enable-debug")
            return SKIP
        if not shutil.which("gdb"):
            print("SKIP: gdb not found")
            return SKIP
        cmd = ["gdb", "-nx", "-q", "-batch",
               "-ex", "set pagination off",
               "-ex", "set confirm off",
               "-ex", "set print thread-events off",
               "-ex", "handle SIGUSR1 SIGPIPE nostop noprint",
               "-x", args.gdb, "--args"] + cmd
        # LeakSanitizer aborts at exit when the process is being ptraced.
        asan = os.environ.get("ASAN_OPTIONS", "")
        asan = f"{asan}:detect_leaks=0" if asan else "detect_leaks=0"
        pypath = os.path.dirname(os.path.abspath(args.gdb))
        old_pypath = os.environ.get("PYTHONPATH", "")
        pypath = f"{pypath}:{old_pypath}" if old_pypath else pypath
        env = dict(os.environ, ASAN_OPTIONS=asan, PYTHONPATH=pypath)
        if "-kernel" in args.qemu:
            idx = args.qemu.index("-kernel")
            if idx + 1 < len(args.qemu):
                env["CHERI_TEST_ELF"] = args.qemu[idx + 1]

    print(" ".join(cmd), flush=True)
    # Own session so a timeout also kills QEMU when it runs under gdb.
    proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, env=env,
                            start_new_session=True)
    try:
        rc = proc.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
        print(f"TIMEOUT after {args.timeout}s")
        return 1
    if rc != 0:
        return rc if rc >= 0 else 128 - rc
    if args.log:
        with open(logfile, errors="replace") as f:
            log = f.read()
        for regex in args.log:
            m = re.search(regex, log, re.MULTILINE)
            print(f"found: {m[0]}" if m else f"MISSING: {regex}")
            rc = rc if m else 1
    return rc


if __name__ == "__main__":
    sys.exit(main())

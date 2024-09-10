#!/bin/bash
set -eu
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
python3  ./riscv_cheri_run_tests.py  dummies.txt  dummy_test >mips64cheri128_tests.xml

#!/bin/bash
targets=${1:-"riscv64cheri riscv32cheri riscv64 riscv32 morello mips64cheri128"}
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
REPO_DIR=$(realpath ${SCRIPT_DIR}/../..)
pushd $REPO_DIR
for i in $targets
do
    scripts/riscv_cheri/build_riscv_cheri $i
    scripts/riscv_cheri/run_tests.sh $i
done
popd

#!/bin/bash
set -eu
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

outfile=$(mktemp)
for i in elfhome/rv64_cheri/*.elf ;
do
    elffile=$(basename $i)
    full_path=$SCRIPT_DIR/${i}
    echo "${elffile} ${qemu_bin} -M virt -nographic -semihosting -bios ${full_path}" >> $outfile
done

for i in elfhome/rv64_cheri/newlib/*.x ;
do
    elffile=$(basename $i)
    full_path=$SCRIPT_DIR/${i}
    echo "${elffile} ${qemu_bin} -M virt -nographic -semihosting -bios ${full_path}" >> $outfile
done

python3 ./riscv_cheri_run_tests.py $outfile riscv64_cheri >riscv64cheri_tests.xml
rm $outfile

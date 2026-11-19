/* SPDX-License-Identifier: BSD-2-Clause */
/* RAM of the riscv virt machine starts at 0x80000000. */
ENTRY(_start)
SECTIONS
{
    . = 0x80000000;
    .text : {
        *(.text.start)
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data : {
        *(.data*)
        *(.sdata*)
    }
    .bss : {
        *(.bss*)
        *(.sbss*)
        *(COMMON)
    }
}

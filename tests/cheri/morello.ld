/* SPDX-License-Identifier: BSD-2-Clause */
/* RAM of the arm virt machine starts at 0x40000000. */
ENTRY(_start)
SECTIONS
{
    . = 0x40000000;
    .text : {
        *(.text.start)
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : {
        *(.bss*)
        *(COMMON)
    }
}

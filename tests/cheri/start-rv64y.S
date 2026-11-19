/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Minimal RV64Y bare-metal startup and semihosting exit stub for C tests.
 * Note: This file was AI-generated for the tests/cheri regression suite.
 */

#define MAX_HARTS  8
#define STACK_SIZE 4096

.section .text.start, "ax"
.globl _start
_start:
    csrr   a0, mhartid
    li     t0, MAX_HARTS
    bgeu   a0, t0, .Lpark

    lla    sp, boot_stacks
    addi   t0, a0, 1
    slli   t0, t0, 12
    add    sp, sp, t0

    bnez   a0, .Lsecondary
    call   test_main
    tail   exit_test

.Lsecondary:
    call   secondary_main
.Lpark:
    wfi
    j      .Lpark

.weak secondary_main
secondary_main:
    ret

/*
 * Exit QEMU with status code in a0 via semihosting SYS_EXIT (0x18).
 * Passes a 16-byte {ADP_Stopped_ApplicationExit (0x20026), code} block in a1.
 */
.globl exit_test
exit_test:
    addi   sp, sp, -16
    andi   sp, sp, -16
    li     t0, 0x20026
    sd     t0, 0(sp)
    sd     a0, 8(sp)
    mv     a1, sp
    li     a0, 0x18
    .option push
    .option norvc
    .balign 16
    slli   x0, x0, 0x1f
    ebreak
    srai   x0, x0, 7
    .option pop
    j      .Lpark

.section .bss
.balign 4096
boot_stacks:
    .space MAX_HARTS * STACK_SIZE

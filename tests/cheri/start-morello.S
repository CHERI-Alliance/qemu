/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Minimal Morello bare-metal startup, PSCI CPU bringup, EL1 exception vector
 * dispatch, and semihosting exit stub for C tests.
 * Note: This file was AI-generated for the tests/cheri regression suite.
 */

#define MAX_CPUS   4
#define STACK_SIZE 4096

.macro MRS_DDC ct
    .inst 0xc29b4120 | (\ct)
.endm
.macro MSR_CVBAR_EL1 ct
    .inst 0xc288c000 | (\ct)
.endm
.macro MRS_CELR_EL1 ct
    .inst 0xc2984020 | (\ct)
.endm
.macro MSR_CELR_EL1 ct
    .inst 0xc2884020 | (\ct)
.endm
.macro SCVALUE cd, cn, xm
    .inst 0xc2c04000 | ((\xm) << 16) | ((\cn) << 5) | (\cd)
.endm

.macro INIT_CPU_STATE
    /* Enable capability instructions (CEN=3) and FP/SIMD (FPEN=3) at EL1. */
    mrs    x0, cpacr_el1
    orr    x0, x0, #(3 << 18)
    orr    x0, x0, #(3 << 20)
    msr    cpacr_el1, x0
    isb
    /* Point both VBAR_EL1 and CVBAR_EL1 at our exception vector table. */
    adr    x1, vectors
    msr    vbar_el1, x1
    MRS_DDC 0
    SCVALUE 0, 0, 1
    MSR_CVBAR_EL1 0
    isb
.endm

.section .text.start, "ax"
.globl _start
_start:
    mrs    x0, mpidr_el1
    and    x0, x0, #0xff
    cbnz   x0, .Lpark

    INIT_CPU_STATE

    adr    x0, boot_stacks
    add    sp, x0, #STACK_SIZE
    bl     test_main
    b      exit_test

.globl _secondary_start
_secondary_start:
    mrs    x19, mpidr_el1
    and    x19, x19, #0xff

    INIT_CPU_STATE

    adr    x0, boot_stacks
    add    x1, x19, #1
    lsl    x1, x1, #12
    add    sp, x0, x1
    mov    x0, x19
    bl     secondary_main
.Lpark:
    wfe
    b      .Lpark

.weak secondary_main
secondary_main:
    ret

/*
 * Start CPU cpu_id (x0) at _secondary_start via PSCI_0_2_FN64_CPU_ON
 * (0xc4000003). Returns the PSCI status in x0 (0 on success).
 */
.globl morello_cpu_on
morello_cpu_on:
    mov    x1, x0
    ldr    x0, =0xc4000003
    adr    x2, _secondary_start
    mov    x3, #0
    hvc    #0
    ret

/*
 * Exit QEMU with status code in w0 via semihosting SYS_EXIT (0x18).
 * Passes a 16-byte {ADP_Stopped_ApplicationExit (0x20026), code} block in x1.
 */
.globl exit_test
exit_test:
    sxtw   x0, w0
    sub    sp, sp, #16
    ldr    x2, =0x20026
    stp    x2, x0, [sp]
    mov    x1, sp
    mov    x0, #0x18
    hlt    #0xf000
    b      .Lpark

/*
 * Default synchronous exception handler: verify a current-EL data abort
 * (EC == 0x25), increment morello_fault_count, and return true (1) so the
 * caller advances ELR_EL1 by 4 bytes past the faulting instruction.
 */
.weak on_sync_exception
on_sync_exception:
    ubfx   x2, x0, #26, #6
    cmp    x2, #0x25
    b.ne   _unexpected_exc_entry
    adr    x2, morello_fault_count
    ldr    x3, [x2]
    add    x3, x3, #1
    str    x3, [x2]
    mov    w0, #1
    ret

_sync_exc_entry:
    sub    sp, sp, #176
    stp    x0, x1, [sp, #0]
    stp    x2, x3, [sp, #16]
    stp    x4, x5, [sp, #32]
    stp    x6, x7, [sp, #48]
    stp    x8, x9, [sp, #64]
    stp    x10, x11, [sp, #80]
    stp    x12, x13, [sp, #96]
    stp    x14, x15, [sp, #112]
    stp    x16, x17, [sp, #128]
    stp    x18, x29, [sp, #144]
    str    x30, [sp, #160]

    mrs    x0, esr_el1
    mrs    x1, elr_el1
    bl     on_sync_exception
    cbz    w0, 1f
    MRS_CELR_EL1 0
    add    x1, x0, #4
    SCVALUE 0, 0, 1
    MSR_CELR_EL1 0
1:
    ldr    x30, [sp, #160]
    ldp    x18, x29, [sp, #144]
    ldp    x16, x17, [sp, #128]
    ldp    x14, x15, [sp, #112]
    ldp    x12, x13, [sp, #96]
    ldp    x10, x11, [sp, #80]
    ldp    x8, x9, [sp, #64]
    ldp    x6, x7, [sp, #48]
    ldp    x4, x5, [sp, #32]
    ldp    x2, x3, [sp, #16]
    ldp    x0, x1, [sp, #0]
    add    sp, sp, #176
    eret

_unexpected_exc_entry:
    mov    w0, #10
    b      exit_test

.balign 2048
vectors:
    .set   i, 0
    .rept  16
    .balign 128
    .if    i == 0 || i == 4
    b      _sync_exc_entry
    .else
    b      _unexpected_exc_entry
    .endif
    .set   i, i + 1
    .endr

.section .bss
.balign 8
.globl morello_fault_count
morello_fault_count:
    .space 8
.balign 4096
boot_stacks:
    .space MAX_CPUS * STACK_SIZE

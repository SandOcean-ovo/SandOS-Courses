/*
 * os_cpu_a_gcc.S: PendSV 上下文切换 (Cortex-M3, GNU 汇编语法)
 *
 * 逻辑与 os_cpu_a.s（armasm 语法，保留作参考，不在 GCC 构建中使用）相同。
 * 硬件在进入异常时自动压入 R0-R3,R12,LR,PC,xPSR；这里只手动保存/恢复 R4-R11。
 * CurrentTCB 为 NULL 表示首次启动，跳过保存直接恢复 NextTCB。
 */
    .syntax unified
    .cpu    cortex-m3
    .thumb

    .extern CurrentTCB
    .extern NextTCB

    .section .text.PendSV_Handler,"ax",%progbits
    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    cpsid   i
    mrs     r0, psp
    isb

    ldr     r2, =CurrentTCB
    ldr     r1, [r2]
    cmp     r1, #0
    beq     RestoreContext

    stmdb   r0!, {r4-r11}
    str     r0, [r1]

RestoreContext:
    ldr     r2, =NextTCB
    ldr     r3, =CurrentTCB
    ldr     r1, [r2]
    str     r1, [r3]
    ldr     r0, [r1]
    ldmia   r0!, {r4-r11}
    msr     psp, r0

    orr     lr, lr, #0x04
    cpsie   i
    bx      lr

    .pool
    .size   PendSV_Handler, .-PendSV_Handler

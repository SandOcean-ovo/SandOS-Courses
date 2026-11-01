/**
 ******************************************************************************
 * @file    os_cpu_a_gcc.S
 * @brief   Cortex-M3 上下文切换汇编（GNU 汇编语法，供 arm-none-eabi-gcc 使用）
 *
 * 与同目录 os_cpu_a.s（armasm 语法）的逻辑完全一致，只是改写成 GNU as 语法：
 *   - 原文件保留作参考，不参与 GCC 构建，也不纳入首版 AC5 兼容承诺；
 *   - AC6 工程使用哪种汇编语法，需在 Keil 工程中显式选择，不在此处用条件编译掩盖。
 *
 * 【硬件自动完成的部分】
 *   Cortex-M3 进入异常时，硬件把 xPSR、PC、LR、R12、R3、R2、R1、R0
 *   依次压入“当前正在使用的栈”（任务运行时为 PSP），共 8 个字。
 *   从异常返回时，硬件按相同顺序把它们弹出。
 *
 * 【本文件（软件）完成的部分】
 *   PendSV 处理函数只需额外保存/恢复 R4-R11 这 8 个寄存器，并切换 PSP：
 *
 *        高地址
 *        +-----------+
 *        |   xPSR    |  <- 硬件压栈（PendSV 进入时）
 *        |   PC      |
 *        |   LR      |
 *        |   R12     |
 *        |   R3      |
 *        |   R2      |
 *        |   R1      |
 *        |   R0      |
 *        +-----------+
 *        |   R11     |  <- 本文件 STMDB 压栈
 *        |   ...     |
 *        |   R4      |  <- 保存后的 PSP，写入 TCB 的第一个成员 stackPtr
 *        +-----------+
 *        低地址
 *
 * 【与内核 C 代码的约定】
 *   - CurrentTCB / NextTCB 是 OS_TCB* 类型的全局变量；
 *   - OS_TCB 的第一个成员必须是保存栈指针的 stackPtr，因此“TCB 地址 == &TCB->stackPtr”，
 *     汇编里 [TCB] 就是读写该任务保存的栈指针；
 *   - CurrentTCB == NULL 表示尚未运行过任何任务（首次启动），此时没有旧上下文可保存。
 ******************************************************************************
 */

    .syntax unified             /* 使用统一语法（UAL），Thumb-2 指令写法 */
    .cpu    cortex-m3           /* 目标内核；用于让汇编器拒绝 M3 不支持的指令 */
    .thumb                      /* Cortex-M 只执行 Thumb 指令 */

    .extern CurrentTCB          /* 当前任务控制块指针，定义在 os_core.c */
    .extern NextTCB             /* 下一个要运行的任务控制块指针，定义在 os_core.c */

    /* 每个函数放进独立的 section，配合 -ffunction-sections / --gc-sections 可裁剪 */
    .section .text.PendSV_Handler,"ax",%progbits
    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func                 /* 让符号地址的最低位为 1（Thumb 函数），向量表才能正确跳转 */

/**
 * @brief  PendSV 异常处理函数：保存当前任务上下文，恢复下一个任务上下文
 *
 * @details 触发方式：C 代码通过 OS_Schedule() 置位 SCB->ICSR 的 PENDSVSET，
 *          PendSV 优先级最低，所以会在所有其他中断处理完之后才执行。
 *
 *          寄存器用法（进入时 R0-R3、R12 已被硬件保存，可以随便用）：
 *            R0 : 当前任务的 PSP / 下一个任务的 PSP
 *            R1 : 当前任务 TCB 指针 / 下一个任务 TCB 指针
 *            R2 : &CurrentTCB 或 &NextTCB
 *            R3 : &CurrentTCB
 *            LR : 进入时为 EXC_RETURN。最后 ORR #4 强制返回到“线程模式 + PSP”，
 *                 因为即使从首次启动路径进入（当时使用 MSP），返回后也必须使用任务栈。
 *
 * @note   全程关中断（CPSID I）以保证切换期间 CurrentTCB/NextTCB 不被改写。
 *         结束前 CPSIE I 无条件开中断——这里是在异常处理内部，且 PendSV 优先级最低，
 *         返回后任务本来就应在开中断状态下运行，不属于“临界区恢复”问题。
 */
PendSV_Handler:
    cpsid   i                   /* 关中断：保护 CurrentTCB/NextTCB 的读写 */
    mrs     r0, psp             /* R0 = 当前任务的栈指针（已含硬件压入的 8 个字） */
    isb                         /* 指令同步屏障：确保上面的状态改变对后续指令生效 */

    ldr     r2, =CurrentTCB     /* R2 = &CurrentTCB（从字面量池取地址） */
    ldr     r1, [r2]            /* R1 = CurrentTCB（当前任务 TCB 指针） */
    cmp     r1, #0              /* 是否首次启动？ */
    beq     RestoreContext      /* 是：没有旧上下文，直接去恢复下一个任务 */

    stmdb   r0!, {r4-r11}       /* 把 R4-R11 压到任务栈，R0 随之下移 32 字节 */
    str     r0, [r1]            /* 保存新的栈指针：TCB->stackPtr = R0 */

RestoreContext:
    ldr     r2, =NextTCB        /* R2 = &NextTCB */
    ldr     r3, =CurrentTCB     /* R3 = &CurrentTCB */
    ldr     r1, [r2]            /* R1 = NextTCB */
    str     r1, [r3]            /* CurrentTCB = NextTCB：从此刻起新任务成为“当前任务” */
    ldr     r0, [r1]            /* R0 = NextTCB->stackPtr（新任务上次保存的栈指针） */
    ldmia   r0!, {r4-r11}       /* 弹出 R4-R11；R0 上移到硬件栈帧起点 */
    msr     psp, r0             /* PSP = R0，异常返回时硬件从这里弹出 R0-R3,R12,LR,PC,xPSR */

    orr     lr, lr, #0x04       /* EXC_RETURN 的 bit2 置 1：返回线程模式并使用 PSP */
    cpsie   i                   /* 开中断 */
    bx      lr                  /* 异常返回：硬件弹栈，跳到新任务的 PC */

    .pool                       /* 放置上面 ldr =符号 用到的字面量池 */
    .size   PendSV_Handler, .-PendSV_Handler

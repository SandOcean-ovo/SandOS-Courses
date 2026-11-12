/**
 ******************************************************************************
 * @file    startup.c
 * @brief   mps2-an385 启动文件：向量表、复位处理、故障报告
 *
 * 启动流程：
 *   1. CPU 复位后从地址 0 读取向量表：第 0 项装入 MSP，第 1 项（Reset_Handler）装入 PC；
 *   2. Reset_Handler 把 .data 初值从 Flash 拷到 RAM，并把 .bss 清零；
 *   3. 调用 __libc_init_array 执行 C 库初始化和 C++/构造函数数组；
 *   4. 调用 main，永不返回。
 *
 * 故障报告：HardFault 把出错时的 PC/LR/xPSR 通过 UART 打印，并输出 "TEST:FAULT"，
 * 测试脚本（tests/scripts/run_qemu.py）看到该标记就判定失败，而不是等到超时。
 ******************************************************************************
 */
#include <stdint.h>
#include "uart.h"
#include "test_support.h"

/* ---- 链接脚本（link.ld）导出的符号，只取地址，不读取其值 ---- */
extern uint32_t _estack;   /**< RAM 顶端，作为初始 MSP */
extern uint32_t _sidata;   /**< .data 初值在 Flash 中的起始地址 */
extern uint32_t _sdata;    /**< .data 在 RAM 中的起始地址 */
extern uint32_t _edata;    /**< .data 在 RAM 中的结束地址 */
extern uint32_t _sbss;     /**< .bss 起始地址 */
extern uint32_t _ebss;     /**< .bss 结束地址 */

extern int  main(void);
extern void PendSV_Handler(void);      /**< 上下文切换，见 os_cpu_a_gcc.S */
extern void OS_Tick_Handler(void);     /**< 内核节拍处理，见 os_core.c */
extern void __libc_init_array(void);   /**< newlib 提供，执行初始化函数数组 */

void Reset_Handler(void);
void Default_Handler(void);
void HardFault_Handler(void);
void SysTick_Handler(void);
/** @brief 默认 SVCall 报告未使用异常；目标测试可用强符号覆盖，内核调度不使用 SVC。 */
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
/** @brief IRQ0 默认仍报告故障；测试可软件挂起此 NVIC IRQ 验证真实异常嵌套，不启用 UART 外设。 */
void UARTRX0_Handler(void) __attribute__((weak, alias("Default_Handler")));

/**
 * @brief 中断向量表
 * @details 由链接脚本放在 .isr_vector 段并置于 Flash 起始地址。
 *          Cortex-M3 的前 16 项为系统异常；值为 0 的项是保留位。
 *          `used` 防止 --gc-sections 把没有被引用的向量表丢掉。
 */
__attribute__((section(".isr_vector"), used))
void (*const g_vectors[])(void) = {
    (void (*)(void))&_estack,   /*  0: 初始 MSP */
    Reset_Handler,              /*  1: 复位 */
    Default_Handler,            /*  2: NMI */
    HardFault_Handler,          /*  3: 硬故障 */
    Default_Handler,            /*  4: MemManage */
    Default_Handler,            /*  5: BusFault */
    Default_Handler,            /*  6: UsageFault */
    0, 0, 0, 0,                 /*  7-10: 保留 */
    SVC_Handler,                /* 11: SVCall，允许测试覆盖默认故障入口 */
    Default_Handler,            /* 12: DebugMonitor */
    0,                          /* 13: 保留 */
    PendSV_Handler,             /* 14: PendSV —— 任务切换 */
    SysTick_Handler,            /* 15: SysTick —— 系统节拍 */
    UARTRX0_Handler,             /* 16: 外部 IRQ0，默认未使能 */
};

/**
 * @brief  复位处理函数：初始化 RAM 并进入 main
 * @note   此时 C 运行环境尚未建立，函数里不能依赖任何已初始化的全局变量。
 */
void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    /* 把有初值的全局变量从 Flash 拷贝到 RAM */
    while (dst < &_edata)
        *dst++ = *src++;

    /* 未初始化的全局变量清零，符合 C 语言对静态存储期对象的要求 */
    for (dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;

    __libc_init_array();
    main();

    for (;;)
        ;   /* main 不应返回；返回则停在这里 */
}

/**
 * @brief  SysTick 中断处理函数：转交给内核节拍处理
 * @note   向量表里的名字固定为 SysTick_Handler，内核的节拍函数叫 OS_Tick_Handler，
 *         所以在板级文件里做一层转发，通用内核不需要知道向量表命名。
 */
void SysTick_Handler(void)
{
    OS_Tick_Handler();
}

/**
 * @brief  以 8 位十六进制输出一个 32 位数（大写，无 0x 前缀）
 * @param  v 要输出的数值
 */
static void put_hex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4)
        uart_putc("0123456789ABCDEF"[(v >> i) & 0xF]);
}

/**
 * @brief  故障报告：打印出错现场并停机
 * @param  frame 异常发生时自动压栈的栈帧指针，布局为
 *               [0]=R0 [1]=R1 [2]=R2 [3]=R3 [4]=R12 [5]=LR [6]=PC [7]=xPSR
 * @param  name  故障名称字符串
 * @note   输出的 "TEST:FAULT" 是测试脚本识别故障的标记，不要随意改名。
 *         `used` 保证符号不被裁剪，因为 HardFault_Handler 在内联汇编里通过名字跳转。
 */
__attribute__((used)) void Fault_Report(uint32_t *frame, const char *name)
{
    uart_puts("\r\nFAULT: ");
    uart_puts(name);
    uart_puts(" PC=");
    put_hex(frame[6]);
    uart_puts(" LR=");
    put_hex(frame[5]);
    uart_puts(" xPSR=");
    put_hex(frame[7]);
    uart_puts("\r\nTEST:FAULT\r\n");
    test_exit(1);
}

/**
 * @brief  硬故障处理函数：找出故障时使用的栈，再交给 Fault_Report
 * @details 必须是 naked 函数：不能让编译器生成序言，否则会改动栈指针，
 *          找到的就不是硬件压入的栈帧。
 *
 *          汇编步骤：
 *            1. 测试 LR(EXC_RETURN) 的 bit2：0 表示故障前用 MSP，1 表示用 PSP；
 *            2. 把对应的栈指针放入 R0，作为 Fault_Report 的第一个参数；
 *            3. 把名字字符串地址放入 R1，作为第二个参数；
 *            4. 直接 b 跳转（不返回）。
 */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4\n"              /* EXC_RETURN bit2：故障前使用的栈 */
        "ite eq\n"
        "mrseq r0, msp\n"           /* bit2 == 0：MSP */
        "mrsne r0, psp\n"           /* bit2 == 1：PSP */
        "ldr r1, =1f\n"             /* R1 = 下面名字字符串的地址 */
        "b Fault_Report\n"
        ".pool\n"                   /* 放置 ldr = 用到的字面量 */
        "1: .asciz \"HardFault\"\n"
        ".align 2\n");
}

/**
 * @brief  未使用异常的默认处理：报告并停机
 * @note   没有栈帧信息，只输出固定文字和 "TEST:FAULT"，足以让测试判失败。
 */
void Default_Handler(void)
{
    uart_puts("\r\nFAULT: unexpected exception\r\nTEST:FAULT\r\n");
    test_exit(1);
}

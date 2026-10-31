/* mps2-an385 启动文件：向量表、Reset_Handler、故障报告 */
#include <stdint.h>
#include "uart.h"

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern int main(void);
extern void PendSV_Handler(void);
extern void OS_Tick_Handler(void);
extern void __libc_init_array(void);

void Reset_Handler(void);
void Default_Handler(void);
void HardFault_Handler(void);
void SysTick_Handler(void);

__attribute__((section(".isr_vector"), used))
void (*const g_vectors[])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    Default_Handler,        /* NMI */
    HardFault_Handler,
    Default_Handler,        /* MemManage */
    Default_Handler,        /* BusFault */
    Default_Handler,        /* UsageFault */
    0, 0, 0, 0,
    Default_Handler,        /* SVCall */
    Default_Handler,        /* DebugMon */
    0,
    PendSV_Handler,
    SysTick_Handler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;
    __libc_init_array();
    main();
    for (;;)
        ;
}

void SysTick_Handler(void)
{
    OS_Tick_Handler();
}

static void put_hex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4)
        uart_putc("0123456789ABCDEF"[(v >> i) & 0xF]);
}

/* 故障时用 UART 输出 PC/LR 等，并以 TEST:FAULT 结尾，便于测试脚本识别 */
__attribute__((used)) void Fault_Report(uint32_t *frame, const char *name)
{
    uart_puts("\r\nFAULT: "); uart_puts(name);
    uart_puts(" PC="); put_hex(frame[6]);
    uart_puts(" LR="); put_hex(frame[5]);
    uart_puts(" xPSR="); put_hex(frame[7]);
    uart_puts("\r\nTEST:FAULT\r\n");
    for (;;)
        ;
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "ldr r1, =1f\n"
        "b Fault_Report\n"
        ".pool\n"
        "1: .asciz \"HardFault\"\n"
        ".align 2\n");
}

void Default_Handler(void)
{
    uart_puts("\r\nFAULT: unexpected exception\r\nTEST:FAULT\r\n");
    for (;;)
        ;
}

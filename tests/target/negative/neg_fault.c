/**
 * @file    neg_fault.c
 * @brief   负例 NEG-03：执行未定义指令，触发 HardFault
 *
 * 目的：验证故障处理（startup.c 中的 HardFault_Handler / Fault_Report）
 *       能输出 "TEST:FAULT"，并且运行脚本把它判为失败，而不是误判通过或卡到超时。
 * 预期：输出 "FAULT: HardFault PC=... LR=... xPSR=..."，
 *       脚本打印 "RESULT: FAIL saw TEST:FAULT"，退出码 1。
 *
 * 为什么会进入 HardFault：UsageFault 默认没有使能，未定义指令产生的 UsageFault
 * 会升级为 HardFault。
 */
#include "uart.h"

/** @brief 入口：先输出一行，再执行 udf 指令触发故障 */
int main(void)
{
    uart_init();
    uart_puts("about to fault\r\n");

    __asm volatile("udf #0");       /* 永久未定义的指令 */

    uart_puts("TEST:PASS\r\n");     /* 不应到达；若到达脚本会误判，说明故障没有发生 */
    for (;;)
        ;
}

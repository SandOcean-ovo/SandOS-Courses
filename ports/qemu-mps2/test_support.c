/** @file test_support.c
 * @brief UART 结果与 Arm SYS_EXIT_EXTENDED 退出码。
 * 依据 ABI 2025Q1 semihosting §6.6；仅用于启用 semihosting 的 QEMU 工程。
 */
#include "test_support.h"
#include "os_board.h"
#include "uart.h"
#include <stdio.h>

static unsigned failures;

void test_begin(const char *id)
{
    uart_init();
    failures = 0;
    printf("CASE %s\n", id);
}

void test_check(int condition, const char *name)
{
    if (!condition)
        failures++;
    printf("CHECK %s %s\n", name, condition ? "PASS" : "FAIL");
}

__attribute__((noreturn)) void test_exit(uint32_t status)
{
    /* R0=操作号，R1=两字参数块地址；T32 使用 BKPT 0xAB。
     * 直接使用 0x20，要求测试环境实现扩展退出；不把本路径承诺为真机行为。 */
    const uint32_t block[2] = {0x20026U, status};
    register uint32_t operation __asm("r0") = 0x20U;
    register const uint32_t *argument __asm("r1") = block;
    __asm volatile("bkpt 0xab" : "+r"(operation), "+r"(argument) : : "memory");
    for (;;)
        ; /* 若调试器选择继续，运行器必须超时，不能假装已经退出。 */
}

__attribute__((noreturn)) void test_finish(void)
{
    __disable_irq();
    SysTick->CTRL = 0;
    printf("TEST:%s\n", failures ? "FAIL" : "PASS");
    fflush(stdout);
    test_exit(failures ? 1U : 0U);
}

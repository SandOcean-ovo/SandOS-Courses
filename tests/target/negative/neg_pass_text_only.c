/**
 * @file    neg_pass_text_only.c
 * @brief   负例 NEG-04：先输出 TEST:PASS，随后发生 HardFault
 *
 * 目的：验证运行脚本不会“看到 TEST:PASS 就立即放行”。
 *       脚本必须等待目标自然退出并排除故障；仅有 PASS 标记不能通过。
 * 预期：脚本打印 "RESULT: FAIL saw TEST:FAULT"，退出码 1。
 *
 * 注意：这里的 PASS 输出是故意的“假通过”，不代表测试真的通过。
 */
#include "uart.h"
#include "test_support.h"

/** @brief 入口：先输出通过标记，再触发未定义指令故障 */
int main(void)
{
    test_begin("NEG-04");
    uart_puts("TEST:PASS\r\n");
    __asm volatile("udf #0");
    for (;;)
        ;
}

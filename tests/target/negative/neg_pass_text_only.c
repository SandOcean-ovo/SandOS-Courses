/**
 * @file    neg_pass_text_only.c
 * @brief   负例 NEG-04：先输出 TEST:PASS，随后发生 HardFault
 *
 * 目的：验证运行脚本不会“看到 TEST:PASS 就立即放行”。
 *       脚本在看到 PASS 后还会继续观察一小段时间（GRACE），期间出现故障/断言仍判失败。
 * 预期：脚本打印 "RESULT: FAIL saw TEST:FAULT after TEST:PASS"，退出码 1。
 *
 * 注意：这里的 PASS 输出是故意的“假通过”，不代表测试真的通过。
 */
#include "uart.h"

/** @brief 入口：先输出通过标记，再触发未定义指令故障 */
int main(void)
{
    uart_init();
    uart_puts("TEST:PASS\r\n");
    __asm volatile("udf #0");
    for (;;)
        ;
}

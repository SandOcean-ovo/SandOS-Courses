/**
 * @file    neg_fail.c
 * @brief   负例 NEG-02：程序主动报告失败
 *
 * 目的：验证运行脚本能识别 "TEST:FAIL" 标记并立即判失败，而不是等到超时。
 * 预期：tests/scripts/run_qemu.py 打印 "RESULT: FAIL saw TEST:FAIL"，退出码 1。
 */
#include "uart.h"
#include "test_support.h"

/** @brief 入口：输出失败标记后停机 */
int main(void)
{
    test_begin("NEG-02");
    uart_puts("TEST:FAIL\r\n");
    test_exit(1);
}

/**
 * @file    neg_hang.c
 * @brief   负例 NEG-01：程序启动后没有任何输出，直接死循环
 *
 * 目的：验证运行脚本不会把“卡死/无输出”当作通过。
 * 预期：tests/scripts/run_qemu.py 超时，打印 "RESULT: TIMEOUT"，退出码 2。
 */

/** @brief 入口：不初始化 UART，不输出任何内容，永远不返回 */
int main(void)
{
    for (;;)
        ;
}

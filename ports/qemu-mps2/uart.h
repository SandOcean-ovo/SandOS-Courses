/**
 ******************************************************************************
 * @file    uart.h
 * @brief   CMSDK APB UART0 最小发送驱动接口（QEMU mps2-an385）
 *
 * 只支持发送，不使用中断。测试脚本通过读取 QEMU 的串口输出判定结果，
 * 所以这里是测试框架的“输出通道”。
 ******************************************************************************
 */
#ifndef UART_H
#define UART_H

/**
 * @brief  初始化 UART0 发送功能
 * @note   必须在任何 uart_putc / uart_puts / printf 之前调用一次。
 */
void uart_init(void);

/**
 * @brief  发送一个字符（阻塞，直到发送 FIFO 有空位）
 * @param  c 要发送的字符
 */
void uart_putc(char c);

/**
 * @brief  发送一个以 '\0' 结尾的字符串（不自动补换行）
 * @param  s 字符串指针，不得为 NULL
 */
void uart_puts(const char *s);

#endif /* UART_H */

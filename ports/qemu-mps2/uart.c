/**
 ******************************************************************************
 * @file    uart.c
 * @brief   CMSDK APB UART0 最小发送驱动（QEMU mps2-an385）
 *
 * 寄存器布局（ARM CMSDK APB UART，基地址 0x40004000）：
 *   偏移 0x00 DATA     —— 写入即发送一个字节
 *   偏移 0x04 STATE    —— bit0 = TX 缓冲区满
 *   偏移 0x08 CTRL     —— bit0 = TX 使能
 *   偏移 0x10 BAUDDIV  —— 波特率分频，QEMU 模型要求 >= 16
 ******************************************************************************
 */
#include <stdint.h>
#include "uart.h"

/** UART0 寄存器基地址（mps2-an385 内存映射） */
#define UART0_BASE          0x40004000UL
/** 数据寄存器：写入发送 */
#define UART_DATA           (*(volatile uint32_t *)(UART0_BASE + 0x00))
/** 状态寄存器 */
#define UART_STATE          (*(volatile uint32_t *)(UART0_BASE + 0x04))
/** 控制寄存器 */
#define UART_CTRL           (*(volatile uint32_t *)(UART0_BASE + 0x08))
/** 波特率分频寄存器 */
#define UART_BAUDDIV        (*(volatile uint32_t *)(UART0_BASE + 0x10))
/** STATE 的 bit0：发送缓冲区满 */
#define UART_STATE_TXFULL   1U
/** CTRL 的 bit0：发送使能 */
#define UART_CTRL_TXEN      1U

/**
 * @brief  初始化 UART0：设置分频并使能发送
 * @note   QEMU 的 CMSDK UART 模型不按波特率限速，BAUDDIV 只需满足 >= 16；
 *         接到真实硬件时必须按时钟和目标波特率重新计算。
 */
void uart_init(void)
{
    UART_BAUDDIV = 16U;
    UART_CTRL    = UART_CTRL_TXEN;
}

/**
 * @brief  发送一个字符
 * @param  c 要发送的字符
 * @note   忙等 TX 缓冲区有空位。寄存器用 volatile 访问，防止编译器把轮询优化掉。
 */
void uart_putc(char c)
{
    while (UART_STATE & UART_STATE_TXFULL)
        ;
    UART_DATA = (uint32_t)(uint8_t)c;
}

/**
 * @brief  发送字符串
 * @param  s 以 '\0' 结尾的字符串
 */
void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

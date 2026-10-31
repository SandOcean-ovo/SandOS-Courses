/* CMSDK APB UART0 (mps2-an385: 0x40004000) 的最小发送驱动 */
#include <stdint.h>
#include "uart.h"

#define UART0_BASE      0x40004000UL
#define UART_DATA       (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_STATE      (*(volatile uint32_t *)(UART0_BASE + 0x04))
#define UART_CTRL       (*(volatile uint32_t *)(UART0_BASE + 0x08))
#define UART_BAUDDIV    (*(volatile uint32_t *)(UART0_BASE + 0x10))
#define UART_STATE_TXFULL 1U

void uart_init(void)
{
    UART_BAUDDIV = 16U;   /* QEMU 模型要求 >= 16，波特率在模型中不起作用 */
    UART_CTRL    = 1U;    /* TX 使能 */
}

void uart_putc(char c)
{
    while (UART_STATE & UART_STATE_TXFULL)
        ;
    UART_DATA = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

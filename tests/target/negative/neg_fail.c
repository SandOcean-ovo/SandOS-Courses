/* 负例：主动报告失败。run_qemu.py 必须报 FAIL（退出码 1） */
#include "uart.h"
int main(void) { uart_init(); uart_puts("TEST:FAIL\r\n"); for (;;) ; }

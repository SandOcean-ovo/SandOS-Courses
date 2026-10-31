/* 负例：执行未定义指令触发 HardFault。必须被故障处理识别为 FAIL（退出码 1） */
#include "uart.h"
int main(void)
{
    uart_init();
    uart_puts("about to fault\r\n");
    __asm volatile("udf #0");              /* 未定义指令 -> UsageFault 未使能 -> 升级为 HardFault */
    uart_puts("TEST:PASS\r\n");              /* 不应到达 */
    for (;;) ;
}

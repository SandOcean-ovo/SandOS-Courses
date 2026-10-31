/* 负例：先输出 TEST:PASS 再发生故障；按"出现故障即失败"的规则，应判 FAIL。
   用来确认判定不会只看到 PASS 就放行。 */
#include "uart.h"
int main(void)
{
    uart_init();
    uart_puts("TEST:PASS\r\n");
    __asm volatile("udf #0");
    for (;;) ;
}

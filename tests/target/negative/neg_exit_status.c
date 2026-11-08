/** @file neg_exit_status.c @brief NEG-06：有 PASS 但目标退出 7，必须失败。 */
#include "test_support.h"
#include "uart.h"
int main(void)
{
    test_begin("NEG-06");
    uart_puts("TEST:PASS\n");
    test_exit(7);
}

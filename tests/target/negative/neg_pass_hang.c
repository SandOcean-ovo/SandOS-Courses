/** @file neg_pass_hang.c @brief NEG-05：有 PASS 但不退出，必须超时。 */
#include "test_support.h"
#include "uart.h"
int main(void)
{
    test_begin("NEG-05");
    uart_puts("TEST:PASS\n");
    for (;;) ;
}

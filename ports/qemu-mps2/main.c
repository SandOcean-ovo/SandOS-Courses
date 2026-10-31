/* 阶段 1 起步：两个任务交替打印，验证启动、UART、PendSV 与 SysTick 路径 */
#include "os_core.h"
#include "uart.h"
#include <stdio.h>

static OS_TCB tcb_a, tcb_b;
static uint32_t stack_a[256], stack_b[256];

static void task_a(void *p) { (void)p; for (int i = 0; i < 3; i++) { printf("A%d\n", i); OS_Delay(10); } printf("TEST:PASS\n"); for (;;) OS_Delay(100); }
static void task_b(void *p) { (void)p; for (int i = 0; i < 3; i++) { printf("B%d\n", i); OS_Delay(10); } for (;;) OS_Delay(100); }

int main(void)
{
    uart_init();
    printf("SandOS qemu-mps2 boot\n");
    OS_Init();
    OS_TaskCreate(&tcb_a, task_a, NULL, stack_a, 256, 1);
    OS_TaskCreate(&tcb_b, task_b, NULL, stack_b, 256, 1);
    OS_StartScheduler();
    return 0;
}

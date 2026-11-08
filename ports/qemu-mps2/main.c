/**
 ******************************************************************************
 * @file    main.c
 * @brief   QEMU 冒烟测试：两个同优先级任务交替打印
 *
 * 这个程序是阶段 1 的第一个可运行用例（用例编号 SMOKE-01），用来证明：
 *   1. 启动文件、链接脚本、UART 输出可用；
 *   2. OS_StartScheduler 能通过 PendSV 首次启动第一个任务；
 *   3. OS_Delay 让出 CPU 后，SysTick 能在延时到期时把任务重新唤醒并切换。
 *
 * 判定规则（由 tests/scripts/run_qemu.py 执行）：
 *   - 输出完整 CASE/TEST:PASS 行，并通过 semihosting 自然退出 0；
 *   - A0、B0、A2 必须按此顺序出现。
 *   两个任务优先级相同，同一个 tick 被唤醒时谁先打印不固定，
 *   所以只断言每个任务自己的先后顺序，不断言 A 与 B 的交错顺序。
 *
 * 不能证明的事：任务栈帧内容是否正确、临界区行为、真机时序。
 ******************************************************************************
 */
#include "os_core.h"
#include "uart.h"
#include "test_support.h"
#include <stdio.h>

/** 每个任务的栈深度（单位：uint32_t 个数，即 1 KB） */
#define TASK_STACK_WORDS    256U
/** 两个任务共用的优先级（数值越小优先级越高；0 保留，最大值给空闲任务） */
#define TASK_PRIO           1U
/** 每个任务循环的次数 */
#define LOOP_COUNT          3
/** 每次循环后延时的 tick 数（1 tick = 1 ms，见 OS_Init_Timer(1)） */
#define LOOP_DELAY_TICKS    10U

static OS_TCB   tcb_a, tcb_b;                    /**< 两个任务的控制块（静态分配） */
static uint32_t stack_a[TASK_STACK_WORDS];       /**< 任务 A 的栈 */
static uint32_t stack_b[TASK_STACK_WORDS];       /**< 任务 B 的栈 */

/**
 * @brief  任务 A：循环打印 "A<i>"，打印完成后输出 TEST:PASS
 * @param  p 任务参数（未使用）
 * @note   完成后输出结果并通过 semihosting 退出 QEMU，不让任务函数返回。
 */
static void task_a(void *p)
{
    (void)p;
    for (int i = 0; i < LOOP_COUNT; i++)
    {
        printf("A%d\n", i);
        OS_Delay(LOOP_DELAY_TICKS);
    }
    test_finish();
}

/**
 * @brief  任务 B：循环打印 "B<i>"
 * @param  p 任务参数（未使用）
 */
static void task_b(void *p)
{
    (void)p;
    for (int i = 0; i < LOOP_COUNT; i++)
    {
        printf("B%d\n", i);
        OS_Delay(LOOP_DELAY_TICKS);
    }
    for (;;)
        OS_Delay(100);
}

/**
 * @brief  程序入口：初始化 UART 与内核，创建两个任务并启动调度器
 * @return 不会返回（OS_StartScheduler 不返回）
 */
int main(void)
{
    test_begin("SMOKE-01");
    printf("SandOS qemu-mps2 boot\n");

    OS_Init();      /* 初始化内核数据结构并创建空闲任务，必须先于 OS_TaskCreate */
    OS_TaskCreate(&tcb_a, task_a, NULL, stack_a, TASK_STACK_WORDS, TASK_PRIO);
    OS_TaskCreate(&tcb_b, task_b, NULL, stack_b, TASK_STACK_WORDS, TASK_PRIO);
    OS_StartScheduler();
    return 0;
}

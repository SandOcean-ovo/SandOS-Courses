/** @file delay_wake.c @brief SCHED-03：高优先级任务延时到期后抢占忙等低优先级任务。 */
#include "test_events.h"
static OS_TCB high, low;
static uint32_t stack_high[256], stack_low[256];
static volatile unsigned low_started;
static uint32_t before_delay;

static void task_high(void *unused)
{
    (void)unused;
    event_record(1);
    before_delay = g_SystemTickCount;
    OS_Delay(3);
    event_record(3);
    const uint32_t expected[] = {1, 2, 3};
    event_verify(expected, 3);
    test_check(low_started && events[2].tick - before_delay >= 3,
               "delay_boundaries_elapsed_and_low_ran");
    test_check(CurrentTCB == &high && high.State == TASK_READY &&
               DelayList.Head == NULL, "woken_high_preempts_busy_low");
    test_finish();
}

static void task_low(void *unused)
{
    (void)unused;
    event_record(2);
    low_started = 1;
    for (;;) ; /* 由 tick 唤醒高优先级任务；低任务没有主动让出。 */
}

int main(void)
{
    test_begin("SCHED-03");
    OS_Init();
    test_check(OS_TaskCreate(&high, task_high, NULL, stack_high, 256, 1) == OS_OK, "create_high");
    test_check(OS_TaskCreate(&low, task_low, NULL, stack_low, 256, 2) == OS_OK, "create_low");
    OS_StartScheduler();
}

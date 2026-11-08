/** @file task_yield.c @brief SCHED-01：无 tick 时主动让出，不进入延时队列。 */
#include "test_events.h"
static OS_TCB a, b;
static uint32_t stack_a[256], stack_b[256];
static uint32_t start_tick;

static void task_a(void *unused)
{
    (void)unused;
    SysTick->CTRL = 0;
    /* 清除可能已挂起的 tick，使整个让出序列没有时间推进。 */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    start_tick = g_SystemTickCount;
    event_record(1);
    OS_Delay(0);
    event_record(3);
    OS_Delay(0);
    event_record(5);
    OS_Delay(10); /* 留下一个真实延时者，检查单独就绪的 B 不会被误挂起。 */
    test_check(0, "delayed_a_must_not_resume_without_tick");
    test_finish();
}

static void task_b(void *unused)
{
    (void)unused;
    event_record(2);
    /* 原实现会把 A 标为 BLOCKED；先保留这条失败复现，避免等到卡死。 */
    if (a.State != TASK_READY || DelayList.Head != NULL)
    {
        test_check(0, "yield_keeps_a_ready_and_delay_list_empty");
        test_finish();
    }
    OS_Delay(0);
    event_record(4);
    OS_Delay(0);
    event_record(6);
    test_check(a.State == TASK_BLOCKED && DelayList.Head == &a && DelayList.Tail == &a,
               "only_positive_delay_is_queued");
    OS_Delay(0); /* B 无同优先级就绪伙伴，应继续运行，空闲任务不替代 B。 */
    event_record(7);
    test_check(CurrentTCB == &b && b.State == TASK_READY &&
               ReadyList[1].Head == &b && ReadyList[1].Tail == &b &&
               g_PrioMap == ((1U << 1) | (1U << (OS_MAX_PRIO - 1))),
               "singleton_yield_keeps_ready_membership");
    test_check(g_SystemTickCount == start_tick, "yield_does_not_need_tick");
    const uint32_t expected[] = {1, 2, 3, 4, 5, 6, 7};
    event_verify(expected, 7);
    test_finish();
}

int main(void)
{
    test_begin("SCHED-01");
    OS_Init();
    test_check(OS_TaskCreate(&a, task_a, NULL, stack_a, 256, 1) == OS_OK, "create_a");
    test_check(OS_TaskCreate(&b, task_b, NULL, stack_b, 256, 1) == OS_OK, "create_b");
    OS_StartScheduler();
}

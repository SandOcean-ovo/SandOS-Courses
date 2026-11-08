/** @file tick_preempt.c @brief SCHED-02：两个忙等任务不调用阻塞/让出 API，由周期 tick 轮转。 */
#include "test_events.h"
static OS_TCB a, b;
static uint32_t stack_a[256], stack_b[256];
static volatile unsigned b_started, a_resumed;

static void task_a(void *unused)
{
    (void)unused;
    event_record(1);
    while (!b_started) ;
    event_record(3);
    a_resumed = 1;
    for (;;) ;
}

static void task_b(void *unused)
{
    (void)unused;
    event_record(2);
    b_started = 1;
    while (!a_resumed) ;
    event_record(4);
    const uint32_t expected[] = {1, 2, 3, 4};
    event_verify(expected, 4);
    test_check(events[1].tick > events[0].tick &&
               events[2].tick > events[1].tick && events[3].tick > events[2].tick,
               "each_rotation_follows_tick");
    test_finish();
}

int main(void)
{
    test_begin("SCHED-02");
    OS_Init();
    test_check(OS_TaskCreate(&a, task_a, NULL, stack_a, 256, 1) == OS_OK, "create_a");
    test_check(OS_TaskCreate(&b, task_b, NULL, stack_b, 256, 1) == OS_OK, "create_b");
    OS_StartScheduler();
}

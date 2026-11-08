/**
 * @file sem_wait_fifo.c
 * @brief SEM-02：真实任务阻塞、FIFO 交接和唤醒后抢占。
 * @note 高优先级 B 先在 gate 上等待，使较低优先级 A 先进入被测等待队列；
 *       后到的 B 不得越过 A。关闭 tick，使顺序由阻塞与 post 唤醒决定。
 */
#include "test_events.h"

static OS_Sem semaphore, gate;
static OS_TCB a, b, producer;
static uint32_t stack_a[256], stack_b[256], stack_producer[256];
static volatile OS_Status status_a, status_b, status_gate;

/** @brief A（优先级 2）先等待被测信号量；获得许可后记录并阻塞。 @param unused 未使用。 */
static void task_a(void *unused)
{
    (void)unused;
    event_record(1);
    status_a = OS_SemWait(&semaphore);
    event_record(4);
    for (;;)
        OS_Delay(100);
}

/** @brief B（优先级 1）经 gate 控制后到达，测试 FIFO 是否独立于优先级。 @param unused 未使用。 */
static void task_b(void *unused)
{
    (void)unused;
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    status_gate = OS_SemWait(&gate);
    event_record(2);
    status_b = OS_SemWait(&semaphore);
    event_record(6);
    for (;;)
        OS_Delay(100);
}

/** @brief Producer（优先级 3）逐次释放许可，检查等待到就绪的迁移。 @param unused 未使用。 */
static void task_producer(void *unused)
{
    (void)unused;
    test_check(OS_SemPost(&gate) == OS_OK && status_gate == OS_OK, "release_b_into_wait_queue");
    test_check(semaphore.WaitList.Head == &a && semaphore.WaitList.Tail == &b &&
               a.Next == &b && b.Prev == &a && a.State == TASK_BLOCKED &&
               b.State == TASK_BLOCKED && semaphore.count == 0,
               "a_then_higher_priority_b_blocked_fifo");
    event_record(3);
    OS_Status first = OS_SemPost(&semaphore);
    event_record(5);
    test_check(first == OS_OK && status_a == OS_OK && semaphore.count == 0 &&
               semaphore.WaitList.Head == &b && semaphore.WaitList.Tail == &b &&
               b.Prev == NULL && b.Next == NULL && b.State == TASK_BLOCKED,
               "first_post_hands_permit_to_a_without_increment");
    OS_Status second = OS_SemPost(&semaphore);
    event_record(7);
    test_check(second == OS_OK && status_b == OS_OK && semaphore.count == 0 &&
               semaphore.WaitList.Head == NULL && semaphore.WaitList.Tail == NULL,
               "second_post_hands_permit_to_b_without_increment");
    test_check(gate.count == 0 && gate.WaitList.Head == NULL &&
               ReadyList[1].Head == NULL && ReadyList[2].Head == NULL &&
               CurrentTCB == &producer &&
               g_PrioMap == ((1U << 3) | (1U << (OS_MAX_PRIO - 1))),
               "final_ready_queues_and_bitmap_consistent");
    const uint32_t expected[] = {1, 2, 3, 4, 5, 6, 7};
    event_verify(expected, 7);
    test_check(events[0].tick == events[6].tick, "wakeup_preemption_needs_no_tick");
    test_finish();
}

/** @brief 建立静态对象与三个任务后启动调度器。 @return 不返回。 */
int main(void)
{
    test_begin("SEM-02");
    OS_Init();
    test_check(OS_SemInit(&semaphore, 0) == OS_OK, "init_semaphore");
    test_check(OS_SemInit(&gate, 0) == OS_OK, "init_gate");
    test_check(OS_TaskCreate(&a, task_a, NULL, stack_a, 256, 2) == OS_OK, "create_a");
    test_check(OS_TaskCreate(&b, task_b, NULL, stack_b, 256, 1) == OS_OK, "create_b");
    test_check(OS_TaskCreate(&producer, task_producer, NULL, stack_producer, 256, 3) == OS_OK,
               "create_producer");
    OS_StartScheduler();
}

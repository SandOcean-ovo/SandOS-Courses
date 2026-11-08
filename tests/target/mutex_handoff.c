/**
 * @file mutex_handoff.c
 * @brief MUTEX-01：关闭 tick，以门闩确定 L→H→HH 两次交接和继承。
 * @note 各阶段在真实任务中检查队列与位图；旧实现出错后在破坏链表前退出。
 */
#include "test_events.h"
static OS_Mutex mutex;
static OS_Sem gate_h, gate_hh;
static OS_TCB low, high, highest;
static uint32_t stack_l[256], stack_h[256], stack_hh[256];
static volatile OS_Status status_l, status_h, status_hh;

/** @brief HH 先等待门闩，H 已持锁后才竞争。 @param unused 未使用。 @return 不返回。 @note 顺序由门闩／阻塞确定，tick 已停或由最高优先级任务先停止。 */
static void task_highest(void *unused)
{
    (void)unused;
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    OS_SemWait(&gate_hh);
    event_record(5);
    status_hh = OS_MutexPend(&mutex);
    event_record(7);
    test_check(status_hh == OS_OK && mutex.Owner == &highest && mutex.NestCount == 1 &&
               highest.State == TASK_READY && highest.HeldMutex == &mutex, "second_handoff_owner_ready_depth_one");
    test_check(OS_MutexPost(&mutex) == OS_OK, "hh_final_unlock");
    event_record(8);
    for (;;) OS_Delay(100);
}

/** @brief H 接锁后唤醒 HH，检查状态错误是否导致错误地搬移就绪节点。 @param unused 未使用。 @return 不返回。 @note 顺序由门闩／阻塞确定，tick 已停或由最高优先级任务先停止。 */
static void task_high(void *unused)
{
    (void)unused;
    OS_SemWait(&gate_h);
    event_record(2);
    status_h = OS_MutexPend(&mutex);
    event_record(4);
    test_check(status_h == OS_OK && mutex.Owner == &high && mutex.NestCount == 1,
               "first_handoff_owner_and_depth");
    test_check(high.State == TASK_READY && high.HeldMutex == &mutex && low.HeldMutex == NULL, "handoff_waiter_becomes_ready");
    OS_SemPost(&gate_hh);
    event_record(6);
    int coherent = high.State == TASK_READY && high.Priority == 1 &&
        ReadyList[1].Head == &high && ReadyList[1].Tail == &high &&
        ReadyList[2].Head == NULL && ReadyList[2].Tail == NULL &&
        high.Prev == NULL && high.Next == NULL &&
        g_PrioMap == ((1U << 1) | (1U << 3) | (1U << (OS_MAX_PRIO - 1)));
    test_check(coherent, "second_inheritance_moves_owner_to_correct_ready_queue");
    test_check(mutex.WaitList.Head == &highest && mutex.WaitList.Tail == &highest &&
               highest.State == TASK_BLOCKED, "hh_is_only_blocked_waiter");
    /* 旧实现此处仍在错误就绪队列；再解锁会进一步损坏链表。保留失败后安全结束。 */
    if (!coherent) test_finish();
    test_check(OS_MutexPost(&mutex) == OS_OK, "h_handoff_to_hh");
    event_record(9);
    test_check(high.Priority == 2 && high.State == TASK_READY && mutex.Owner == NULL &&
               mutex.NestCount == 0, "h_priority_restored_and_hh_released");
    for (;;) OS_Delay(100);
}

/** @brief L 建立持锁并验证两轮继承、恢复和最终队列。 @param unused 未使用。 @return 不返回。 @note 顺序由门闩／阻塞确定，tick 已停或由最高优先级任务先停止。 */
static void task_low(void *unused)
{
    (void)unused;
    status_l = OS_MutexPend(&mutex);
    test_check(OS_MutexPend(&mutex) == OS_OK && mutex.NestCount == 2, "l_recursive_depth_two");
    event_record(1);
    test_check(status_l == OS_OK, "l_acquires_first_lock");
    OS_SemPost(&gate_h);
    event_record(3);
    test_check(low.Priority == 2 && low.State == TASK_READY && high.State == TASK_BLOCKED &&
               mutex.WaitList.Head == &high && mutex.WaitList.Tail == &high,
               "first_owner_inherits_h_priority");
    test_check(OS_MutexPost(&mutex) == OS_OK && mutex.NestCount == 1 &&
               mutex.Owner == &low && low.Priority == 2 && low.HeldMutex == &mutex &&
               high.State == TASK_BLOCKED && event_count == 3,
               "recursive_partial_release_keeps_inheritance_and_waiter_blocked");
    test_check(OS_MutexPost(&mutex) == OS_OK, "l_handoff_to_h");
    event_record(10);
    test_check(low.Priority == 3 && low.State == TASK_READY &&
               ReadyList[3].Head == &low && ReadyList[3].Tail == &low &&
               ReadyList[1].Head == NULL && ReadyList[2].Head == NULL &&
               g_PrioMap == ((1U << 3) | (1U << (OS_MAX_PRIO - 1))),
               "final_ready_lists_and_bitmap");
    test_check(mutex.Owner == NULL && mutex.NestCount == 0 &&
               mutex.WaitList.Head == NULL && mutex.WaitList.Tail == NULL &&
               low.HeldMutex == NULL && high.HeldMutex == NULL && highest.HeldMutex == NULL,
               "final_lock_has_no_owner_or_waiters");
    const uint32_t expected[] = {1,2,3,4,5,6,7,8,9,10};
    event_verify(expected, 10);
    test_check(events[0].tick == events[9].tick, "both_handoffs_need_no_tick");
    test_finish();
}

/** @brief 创建静态对象与三任务；真实阻塞驱动顺序。 @return 不返回。 @note 顺序由门闩／阻塞确定，tick 已停或由最高优先级任务先停止。 */
int main(void)
{
    test_begin("MUTEX-01");
    OS_Init();
    test_check(OS_MutexInit(&mutex) == OS_OK, "init_mutex");
    OS_SemInit(&gate_h, 0); OS_SemInit(&gate_hh, 0);
    test_check(OS_TaskCreate(&low, task_low, NULL, stack_l, 256, 3) == OS_OK, "create_l");
    test_check(OS_TaskCreate(&high, task_high, NULL, stack_h, 256, 2) == OS_OK, "create_h");
    test_check(OS_TaskCreate(&highest, task_highest, NULL, stack_hh, 256, 1) == OS_OK, "create_hh");
    OS_StartScheduler();
}

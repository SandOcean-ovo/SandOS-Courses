/**
 * @file isr_wakeup.c
 * @brief ISR-02：非 tick 唤醒与嵌套 ISR 尾部重选，全部顺序由阻塞／异常控制。
 * @note 每层 ISR 使用自己的标记；高任务不得在外层 Handler 返回前执行。
 */
#include "test_events.h"
static OS_Sem high_first, high_second, mid_second, spare;
static OS_Queue queue, scratch;
static uint32_t queue_buffer, scratch_buffer;
static OS_TCB high, mid, low;
static uint32_t hs[256], ms[256], ls[384];
static volatile uint32_t phase, high_progress, mid_progress, outer_active;
static volatile uint32_t outer_ok, inner_ok, outer_ipsr, inner_ipsr;

/** @brief 内层 IRQ 唤醒 H 并选择 H，不能立即越过外层 SVC 运行任务。 @return 无。 @note 标记只归本 ISR 所有。 */
void UARTRX0_Handler(void)
{
    uint8_t woken = FALSE;
    inner_ipsr = __get_IPSR(); event_record(9);
    inner_ok = OS_SemPostFromISR(&high_second, &woken) == OS_OK && woken == TRUE;
    OS_YieldFromISR(woken);
    inner_ok &= NextTCB == &high && CurrentTCB == &low && high_progress == 1 && outer_active;
    event_record(10);
}

/** @brief 单层先唤醒 H／M，第二阶段被 IRQ 抢占后再次按全部就绪任务重选。 @return 无。 @note 不在 Handler 中打印，避免引入非测试异常。 */
void SVC_Handler(void)
{
    uint8_t woken = FALSE;
    uint32_t value = 0xAABBCCDD, out;
    outer_active = 1; outer_ipsr = __get_IPSR();
    if (phase == 0) {
        event_record(2);
        outer_ok = OS_SemPostFromISR(&high_first, &woken) == OS_OK && woken == TRUE;
        outer_ok &= OS_SemPostFromISR(&spare, &woken) == OS_OK && woken == TRUE;
        outer_ok &= OS_QueueSendFromISR(&queue, &value, &woken) == OS_OK && woken == TRUE;
        outer_ok &= OS_QueueSendFromISR(&queue, &value, &woken) == OS_ERR_Q_FULL && woken == TRUE;
        outer_ok &= OS_QueueReceiveFromISR(&scratch, &out, &woken) == OS_ERR_RESOURCE && woken == TRUE;
        OS_YieldFromISR(woken);
        outer_ok &= NextTCB == &high && CurrentTCB == &low && high_progress == 0 && mid_progress == 0;
        OS_TCB *selected = NextTCB;
        OS_YieldFromISR(FALSE);
        outer_ok &= NextTCB == selected && (SCB->ICSR & SCB_ICSR_PENDSVSET_Msk) != 0;
        event_record(3);
    } else {
        event_record(8);
        outer_ok &= OS_SemPostFromISR(&mid_second, &woken) == OS_OK && woken == TRUE;
        OS_YieldFromISR(woken);
        outer_ok &= NextTCB == &mid;
        NVIC_SetPendingIRQ(UARTRX0_IRQn); __DSB(); __ISB();
        outer_ok &= inner_ok && NextTCB == &high;
        outer_ok &= OS_QueueReceiveFromISR(&scratch, &out, &woken) == OS_ERR_RESOURCE && woken == TRUE;
        OS_YieldFromISR(woken);
        outer_ok &= NextTCB == &high && CurrentTCB == &low && high_progress == 1 && mid_progress == 1;
        event_record(11);
    }
    outer_active = 0;
}

/** @brief H 第一轮等 sem，第二轮等 gate，醒后记录并再次阻塞。 @param unused 未用。 @return 不返回。 @note 第一任务关闭 tick。 */
static void task_high(void *unused)
{
    (void)unused; SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    OS_SemWait(&high_first); event_record(4);
    test_check(!outer_active && outer_ok && mid_progress == 0, "high_runs_after_outer_isr_before_mid");
    high_progress = 1;
    OS_SemWait(&high_second); event_record(12);
    test_check(!outer_active && inner_ok && mid_progress == 1, "nested_high_runs_after_interrupt_chain");
    high_progress = 2;
    for (;;) OS_Delay(100);
}

/** @brief M 先等队列，再等独立 gate，用不同资源覆盖两条唤醒路径。 @param unused 未用。 @return 不返回。 @note 队列接收无超时。 */
static void task_mid(void *unused)
{
    uint32_t value = 0;
    (void)unused;
    test_check(OS_QueueReceive(&queue, &value) == OS_OK && value == 0xAABBCCDD,
               "isr_queue_message_received");
    event_record(5); mid_progress = 1;
    OS_SemWait(&mid_second); event_record(13);
    test_check(high_progress == 2 && !outer_active, "nested_mid_runs_after_high");
    mid_progress = 2;
    for (;;) OS_Delay(100);
}

/** @brief 低任务触发两轮中断，并核对事件、位图和等待队列。 @param unused 未用。 @return 不返回。 @note 禁用 tick 后任何唤醒都必须来自 ISR 尾部。 */
static void task_low(void *unused)
{
    (void)unused;
    NVIC_SetPriority(SVCall_IRQn, 4); NVIC_SetPriority(UARTRX0_IRQn, 1);
    NVIC_ClearPendingIRQ(UARTRX0_IRQn); NVIC_EnableIRQ(UARTRX0_IRQn);
    event_record(1); __asm volatile("svc #0" ::: "memory"); event_record(6);
    test_check(outer_ok && high_progress == 1 && mid_progress == 1,
               "single_isr_wakes_both_before_low_resumes");
    phase = 1; event_record(7); __asm volatile("svc #0" ::: "memory"); event_record(14);
    test_check(outer_ok && inner_ok && high_progress == 2 && mid_progress == 2 &&
               outer_ipsr == 11 && inner_ipsr == 16, "nested_isrs_reselect_latest_highest");
    test_check(high_second.WaitList.Head == NULL && mid_second.WaitList.Head == NULL &&
               high_first.WaitList.Head == NULL && queue.WaitReadList.Head == NULL &&
               queue.MsgCount == 0 && spare.count == 1 &&
               ReadyList[1].Head == NULL && ReadyList[2].Head == NULL &&
               g_PrioMap == ((1U << 5) | (1U << (OS_MAX_PRIO - 1))), "final_queues_and_bitmap");
    const uint32_t expected[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    event_verify(expected, 14);
    test_check(events[0].tick == events[13].tick, "all_isr_wakeups_need_no_tick");
    test_finish();
}

/** @brief 创建真实等待者和低优先级触发者。 @return 不返回。 @note 所有静态资源在启动前初始化。 */
int main(void)
{
    test_begin("ISR-02"); OS_Init();
    OS_SemInit(&high_first, 0); OS_SemInit(&high_second, 0);
    OS_SemInit(&mid_second, 0); OS_SemInit(&spare, 0);
    OS_QueueInit(&queue, &queue_buffer, 4, 1); OS_QueueInit(&scratch, &scratch_buffer, 4, 1);
    test_check(OS_TaskCreate(&high, task_high, NULL, hs, 256, 1) == OS_OK, "create_high");
    test_check(OS_TaskCreate(&mid, task_mid, NULL, ms, 256, 2) == OS_OK, "create_mid");
    test_check(OS_TaskCreate(&low, task_low, NULL, ls, 384, 5) == OS_OK, "create_low");
    OS_StartScheduler();
}

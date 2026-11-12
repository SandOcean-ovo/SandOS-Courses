/**
 * @file isr_priority.c
 * @brief ISR-05：同／低优先级唤醒不请求切换，TRUE 重选也不轮转当前优先级。
 * @note gate 让同／低优先级任务先等待；关闭 tick，以显式 Delay(0) 验证随后轮转。
 */
#include "os_core.h"
#include "test_support.h"
static OS_Sem gate, equal_sem, lower_sem;
static OS_TCB active, equal, lower, producer;
static uint32_t a_stack[256], e_stack[256], l_stack[256], p_stack[256];
static volatile unsigned equal_ran, lower_ran, irq_ok;

/** @brief 唤醒同级和低级等待者，局部 FALSE 必须保持，调度重选不制造轮转。 @return 无。 @note 本 ISR 不使能周期 tick。 */
void SVC_Handler(void)
{
    uint8_t woken = FALSE;
    irq_ok = OS_SemPostFromISR(&equal_sem, &woken) == OS_OK && woken == FALSE;
    irq_ok &= OS_SemPostFromISR(&lower_sem, &woken) == OS_OK && woken == FALSE;
    OS_YieldFromISR(woken);
    irq_ok &= (SCB->ICSR & SCB_ICSR_PENDSVSET_Msk) == 0;
    OS_YieldFromISR(TRUE); /* 即使要求重选，当前任务仍为最高优先级队头。 */
    irq_ok &= NextTCB == &active && CurrentTCB == &active && ReadyList[2].Head == &active &&
              ReadyList[2].Tail == &equal && (SCB->ICSR & SCB_ICSR_PENDSVSET_Msk) == 0;
}

/** @brief 先阻塞，让另两任务准备等待；唤醒后触发 SVC 并显式轮转。 @param unused 未用。 @return 不返回。 @note 不把同级唤醒等同高优先级抢占。 */
static void task_active(void *unused)
{
    (void)unused; SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    OS_SemWait(&gate);
    __asm volatile("svc #0" ::: "memory");
    test_check(irq_ok && !equal_ran && !lower_ran, "equal_and_lower_wakeup_do_not_preempt");
    OS_Delay(0);
    test_check(equal_ran && !lower_ran, "equal_runs_only_after_explicit_yield");
    for (;;) OS_Delay(100);
}

/** @brief 同级任务等待 ISR 后记录并阻塞。 @param unused 未用。 @return 不返回。 @note 运行顺序由 gate 控制。 */
static void task_equal(void *unused)
{
    (void)unused; OS_SemWait(&equal_sem); equal_ran = 1;
    for (;;) OS_Delay(100);
}

/** @brief 低级任务等 ISR 后记录并阻塞。 @param unused 未用。 @return 不返回。 @note 不能抢占更高任务。 */
static void task_lower(void *unused)
{
    (void)unused; OS_SemWait(&lower_sem); lower_ran = 1;
    for (;;) OS_Delay(100);
}

/** @brief 释放 gate 并在任务完成后检查等待节点和标记。 @param unused 未用。 @return 不返回。 @note 优先级最低的验证者最后返回。 */
static void task_producer(void *unused)
{
    (void)unused;
    test_check(equal_sem.WaitList.Head == &equal && lower_sem.WaitList.Head == &lower,
               "equal_and_lower_waiters_prepared");
    OS_SemPost(&gate);
    test_check(irq_ok && equal_ran && lower_ran && equal_sem.WaitList.Head == NULL &&
               lower_sem.WaitList.Head == NULL, "both_waiters_run_without_lost_permits");
    test_finish();
}

/** @brief 创建优先级 2／2／3／5 的四任务。 @return 不返回。 @note 静态初始化遵循 M1 范围。 */
int main(void)
{
    test_begin("ISR-05"); OS_Init();
    OS_SemInit(&gate, 0); OS_SemInit(&equal_sem, 0); OS_SemInit(&lower_sem, 0);
    OS_TaskCreate(&active, task_active, NULL, a_stack, 256, 2);
    OS_TaskCreate(&equal, task_equal, NULL, e_stack, 256, 2);
    OS_TaskCreate(&lower, task_lower, NULL, l_stack, 256, 3);
    OS_TaskCreate(&producer, task_producer, NULL, p_stack, 256, 5);
    OS_StartScheduler();
}

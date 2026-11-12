/**
 * @file isr_atomic_fixture.h
 * @brief GDB 在资源或 tick 链表更新窗口挂起高优先级 IRQ，检验保护与真正唤醒。
 * @note 仅通过正常端口寄存器注入 IRQ；不替换内核函数或加入生产测试钩子。
 */
#include "os_core.h"
#include "test_support.h"
extern volatile uint32_t g_CriticalNesting;
static OS_TCB high, low;
static uint32_t hs[256], ls[256];
static OS_Sem sem;
volatile uint8_t g_outer_woken;
volatile uint32_t g_observed_ok, g_irq_seen, g_high_ran, g_finished, g_irq_ipsr;

/** @brief 高优先级 IRQ 观察外层完成状态，再操作同一资源并请求调度。 @return 无。 @note 各层独立局部标记。 */
void UARTRX0_Handler(void)
{
    uint8_t woken = FALSE;
    g_irq_ipsr = __get_IPSR();
    if (ATOMIC_TICK)
        g_observed_ok = high.State == TASK_BLOCKED && sem.WaitList.Head == &high && g_SystemTickCount == 1;
    else
        g_observed_ok = high.State == TASK_READY && sem.WaitList.Head == NULL &&
                        (g_PrioMap & (1U << 1)) && g_outer_woken == TRUE;
    g_observed_ok &= g_CriticalNesting == 0 && __get_PRIMASK() == 0 && CurrentTCB == &low;
    g_observed_ok &= OS_SemPostFromISR(&sem, &woken) == OS_OK;
    OS_YieldFromISR(woken);
    g_irq_seen = 1;
}

/** @brief 资源专项的外层 SVC；tick 专项保留此符号但不会调用。 @return 无。 @note 保护窗口由 GDB 断点控制。 */
void SVC_Handler(void)
{
    OS_SemPostFromISR(&sem, (uint8_t *)&g_outer_woken);
    OS_YieldFromISR(g_outer_woken);
}

/** @brief 高任务消费 ISR 交接许可后记录并让出低任务。 @param unused 未用。 @return 不返回。 @note 关闭周期 tick，使仅一次手动异常。 */
static void task_high(void *unused)
{
    (void)unused; SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    OS_SemWait(&sem); g_high_ran = 1;
    for (;;) OS_Delay(100);
}

/** @brief 触发外层异常并到达 GDB 终点。 @param unused 未用。 @return 不返回。 @note IRQ 必须在高任务执行前完成观察。 */
static void task_low(void *unused)
{
    (void)unused;
    NVIC_SetPriority(SVCall_IRQn, 4); NVIC_SetPriority(UARTRX0_IRQn, 1);
    NVIC_ClearPendingIRQ(UARTRX0_IRQn); NVIC_EnableIRQ(UARTRX0_IRQn);
    if (ATOMIC_TICK) { SCB->ICSR = SCB_ICSR_PENDSTSET_Msk; __DSB(); __ISB(); }
    else __asm volatile("svc #0" ::: "memory");
    g_finished = 0x150CAFE;
    test_finish(); /* GDB 在入口停下，核对观察标记，不依赖 UART PASS。 */
}

/** @brief 初始化两任务及被测 semaphore。 @return 不返回。 @note 测试必须通过指定 GDB 脚本注入 IRQ。 */
int main(void)
{
    test_begin(ATOMIC_CASE); OS_Init(); OS_SemInit(&sem, 0);
    OS_TaskCreate(&high, task_high, NULL, hs, 256, 1);
    OS_TaskCreate(&low, task_low, NULL, ls, 256, 5);
    OS_StartScheduler();
}

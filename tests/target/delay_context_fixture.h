/**
 * @file delay_context_fixture.h
 * @brief GDB 观察 Delay 拒绝时的真实断言现场；三种上下文共用同一组状态快照。
 * @note 只由独立目标 wrapper 包含；不重定义 OS_ASSERT，不修改生产断言函数。
 */
#include "os_core.h"
#include "test_support.h"
extern volatile uint32_t g_CriticalNesting;
static OS_TCB runner, peer;
static uint32_t runner_stack[512], peer_stack[256];
OS_TCB g_task_before;
OS_List g_ready_before[OS_MAX_PRIO], g_delay_before;
OS_TCB *g_next_before;
volatile uint32_t g_attempted, g_returned, g_mask_before, g_nesting_before;
volatile uint32_t g_bitmap_before, g_pending_before, g_expected_ipsr, g_tick_before;

/** @brief 同优先级伙伴使非法 Delay(0) 的队列轮转也可观察。 @param unused 未使用。 @return 不返回。 @note 正确拒绝时它不会得到运行机会。 */
static void task_peer(void *unused)
{
    (void)unused;
    for (;;) __NOP();
}

/** @brief 真实 Handler 模式下调用 Delay。 @return 无。 @note 断言应在改状态前发生。 */
void SVC_Handler(void)
{
    OS_Delay(1);
}

/** @brief 建立调用者环境并抓取调用前状态。 @param unused 未使用。 @return 不返回。 @note GDB 停在断言入口读取内存，不调用目标函数取快照。 */
static void task_runner(void *unused)
{
    (void)unused;
    SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    if (CONTEXT_DELAY_MODE == 0) { OS_EnterCritical(); OS_EnterCritical(); }
    if (CONTEXT_DELAY_MODE == 2) __disable_irq();
    memcpy(&g_task_before, &runner, sizeof(runner));
    memcpy(g_ready_before, ReadyList, sizeof(g_ready_before));
    g_delay_before = DelayList; g_next_before = NextTCB;
    g_mask_before = __get_PRIMASK(); g_nesting_before = g_CriticalNesting;
    g_bitmap_before = g_PrioMap; g_tick_before = g_SystemTickCount;
    g_pending_before = SCB->ICSR & (SCB_ICSR_PENDSVSET_Msk | SCB_ICSR_PENDSTSET_Msk);
    g_expected_ipsr = CONTEXT_DELAY_MODE == 1 ? 11U : 0U;
    g_attempted = 0xC07E0001U;
    if (CONTEXT_DELAY_MODE == 1) __asm volatile("svc #0" ::: "memory");
    else OS_Delay(CONTEXT_DELAY_MODE == 2 ? 0 : 1);
    /* 旧实现若返回，则不会碰到 GDB 的断言断点；运行器必须失败或超时。 */
    g_returned = 1;
    for (;;) __NOP();
}

/** @brief 启动两个真实任务以验证断言发生前的状态一致性。 @return 不返回。 @note 不使用主机异常桩。 */
int main(void)
{
    test_begin(CONTEXT_DELAY_CASE); OS_Init();
    OS_TaskCreate(&runner, task_runner, NULL, runner_stack, 512, 2);
    OS_TaskCreate(&peer, task_peer, NULL, peer_stack, 256, 2);
    OS_StartScheduler();
}

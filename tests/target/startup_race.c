/**
 * @file startup_race.c
 * @brief START-01：链接包装在首次 PendSV 请求前确定性挂起真实 SysTick。
 * @note WRAP=OS_Schedule OS_Tick_Handler；旧内核会在 CurrentTCB 为空时断言。
 */
#include "os_core.h"
#include "test_support.h"
#include <string.h>
extern volatile uint32_t g_CriticalNesting;
void __real_OS_Schedule(void);
void __real_OS_Tick_Handler(void);
static OS_TCB runner, peer;
static uint32_t runner_stack[384], peer_stack[256];
static OS_List ready_before[OS_MAX_PRIO], delay_before;
static OS_TCB task_before, peer_before;
static uint32_t map_before, tick_before;
static OS_TCB *next_before;
volatile uint32_t g_injected, g_early_tick_seen, g_early_tick_intact, g_early_state, g_early_ipsr;
volatile uint32_t g_expect_late_tick, g_live_tick_seen, g_peer_ran, g_finished, g_stopped_tick_seen;

/** @brief 测试 GDB 可调用的 CPU 挂起操作，兼容旧 QEMU 的 PPB 调试器写入限制。 @return 无。 @note main 在屏蔽时调用后清 pending，保留链接符号。 */
__attribute__((noinline)) void startup_test_pend_tick(void)
{
    SCB->ICSR = SCB_ICSR_PENDSTSET_Msk;
    __DSB(); __ISB();
}

/** @brief 仅首次调度请求时注入真实 tick；停止周期源隔离时间竞态。 @return 无。 @note 调用真实端口函数，正常切换其余行为不替换。 */
void __wrap_OS_Schedule(void)
{
    if (!g_injected && CurrentTCB == NULL) {
        SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        memcpy(ready_before, ReadyList, sizeof(ready_before)); delay_before = DelayList;
        memcpy(&task_before, &runner, sizeof(runner)); memcpy(&peer_before, &peer, sizeof(peer));
        map_before = g_PrioMap; tick_before = g_SystemTickCount; next_before = NextTCB;
        g_injected = 1;
        startup_test_pend_tick();
    }
    __real_OS_Schedule();
}

/** @brief 记录真实 SysTick 的启动状态，再调用生产 tick 处理。 @return 无。 @note 内核必须自己拒绝未就绪的启动 tick，夹具不绕过它。 */
void __wrap_OS_Tick_Handler(void)
{
    if (g_injected && CurrentTCB == NULL) {
        g_early_tick_seen++;
        g_early_state = g_OSRunning; g_early_ipsr = __get_IPSR();
        __real_OS_Tick_Handler();
        g_early_tick_intact = CurrentTCB == NULL && NextTCB == next_before &&
            g_SystemTickCount == tick_before && g_PrioMap == map_before &&
            memcmp(ready_before, ReadyList, sizeof(ready_before)) == 0 &&
            memcmp(&delay_before, &DelayList, sizeof(DelayList)) == 0 &&
            memcmp(&task_before, &runner, sizeof(runner)) == 0 &&
            memcmp(&peer_before, &peer, sizeof(peer)) == 0;
    } else {
        if (CurrentTCB != NULL) g_live_tick_seen++;
        else g_stopped_tick_seen++;
        __real_OS_Tick_Handler();
    }
}

/** @brief 同级 peer 只在运行后的 tick 轮转时执行，再阻塞。 @param unused 未用。 @return 不返回。 @note 不依赖周期 tick。 */
static void task_peer(void *unused)
{
    (void)unused; g_peer_ran = 1;
    for (;;) OS_Delay(100);
}

/** @brief 检查被忽略启动 tick、可调度发布以及后续真实 tick。 @param unused 未用。 @return 不返回。 @note GDB 可以额外挂起一个栈就绪窗口的 tick。 */
static void task_runner(void *unused)
{
    (void)unused;
    test_check(g_injected && g_early_tick_seen == 1 && g_early_ipsr == 15, "real_tick_precedes_first_task");
    /* 数值先锁定为 2，使同一复现仍能与修复前头文件一起编译。 */
    test_check(g_early_state == 2 && g_early_tick_intact, "starting_tick_preserves_all_scheduler_state");
    test_check(g_OSRunning == TRUE && CurrentTCB == &runner && __get_IPSR() == 0 &&
               __get_PRIMASK() == 0 && g_CriticalNesting == 0, "first_task_is_running_and_unmasked");
    test_check(__get_PSP() > (uint32_t)runner_stack &&
               __get_PSP() <= (uint32_t)(runner_stack + 384), "first_task_psp_in_own_stack");
    test_check(g_SystemTickCount == g_expect_late_tick && g_live_tick_seen == g_expect_late_tick,
               "only_post_commit_tick_is_counted");
    startup_test_pend_tick();
    test_check(g_SystemTickCount == g_expect_late_tick + 1 && g_peer_ran == 1,
               "running_tick_advances_with_peer_executed");
    test_check(g_OSRunning == TRUE && CurrentTCB == &runner && peer.State == TASK_BLOCKED &&
               ReadyList[2].Head == &runner && g_CriticalNesting == 0,
               "normal_switch_keeps_running_state");
    g_finished = 0x57A47001;
    test_finish();
}

/** @brief 启动前准备两任务并保留可调用的注入 helper。 @return 不返回。 @note main 的屏蔽／清 pending 不改变生产启动过程。 */
int main(void)
{
    test_begin("START-01"); OS_Init();
    uint32_t saved = OS_SaveIRQ(); startup_test_pend_tick();
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk; OS_RestoreIRQ(saved);
    startup_test_pend_tick();
    test_check(g_stopped_tick_seen == 1 && g_SystemTickCount == 0 && g_OSRunning == FALSE &&
               CurrentTCB == NULL && NextTCB == NULL, "stopped_tick_is_ignored");
    test_check(OS_TaskCreate(&runner, task_runner, NULL, runner_stack, 384, 2) == OS_OK, "create_runner");
    test_check(OS_TaskCreate(&peer, task_peer, NULL, peer_stack, 256, 2) == OS_OK, "create_peer");
    OS_StartScheduler();
}

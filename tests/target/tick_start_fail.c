/** @file tick_start_fail.c @brief TICK-02：板级时钟为零时，Start 恢复未启动状态后原生断言。 */
#include "os_core.h"
#include "test_support.h"
#include <string.h>
extern volatile uint32_t g_CriticalNesting;
volatile uint32_t g_attempt, g_clock_reads, g_map_before;
OS_List g_ready_before[OS_MAX_PRIO], g_delay_before;
uint8_t g_pend_priority, g_tick_priority;
uint32_t g_control_before;
/** @brief 注入无效板级时钟。 @return 0。 @note 生产代码必须拒绝，不能创建错误的 tick。 */
uint32_t __wrap_OS_BoardClockHz(void) { g_clock_reads++; return 0; }
/** @brief 准备硬件／就绪表快照，再进入真实启动入口。 @return 不返回。 @note GDB 停在 OS_AssertFailed，不替换断言。 */
int main(void)
{
    test_begin("TICK-02"); OS_Init();
    SysTick->CTRL = 0; SysTick->LOAD = 77; SysTick->VAL = 0;
    NVIC_SetPriority(PendSV_IRQn, 2); NVIC_SetPriority(SysTick_IRQn, 3);
    g_pend_priority = SCB->SHPR[10]; g_tick_priority = SCB->SHPR[11];
    g_control_before = SysTick->CTRL & 7;
    memcpy(g_ready_before, ReadyList, sizeof(g_ready_before)); g_delay_before = DelayList;
    g_map_before = g_PrioMap; g_attempt = 0x71CFA11;
    OS_StartScheduler();
    for (;;) __NOP();
}

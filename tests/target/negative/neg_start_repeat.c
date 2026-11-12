/** @file neg_start_repeat.c @brief STARTNEG-01：已运行任务再次启动应在改动调度状态前断言。 */
#include "os_core.h"
#include "test_support.h"
static OS_TCB task;
static uint32_t stack[256];
/** @brief 已在任务上下文中故意重复启动。 @param unused 未用。 @return 不返回。 @note 保留生产断言。 */
static void entry(void *unused)
{
    (void)unused; SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    OS_StartScheduler();
    test_finish();
}
/** @brief 正常首次启动后才触发误用。 @return 不返回。 @note 不能把启动前状态误当重复启动。 */
int main(void)
{
    test_begin("STARTNEG-01"); OS_Init();
    OS_TaskCreate(&task, entry, NULL, stack, 256, 2);
    OS_StartScheduler();
}

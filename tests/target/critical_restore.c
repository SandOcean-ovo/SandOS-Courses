/**
 * @file critical_restore.c
 * @brief CRIT-01：真实 PRIMASK 的进入前状态、两层嵌套与跨次调用恢复。
 * @note 先验证已屏蔽场景，使旧实现在资源或调度无关的最小路径失败。
 */
#include "os_core.h"
#include "test_support.h"
extern volatile uint32_t g_CriticalNesting;

/** @brief 从指定屏蔽状态进入两层临界区并核对逐层退出。 @param mask 原 PRIMASK。 @return 无。 @note 最后显式开中断，只为隔离下一组试验。 */
static void trial(uint32_t mask)
{
    __set_PRIMASK(mask);
    OS_EnterCritical();
    test_check(__get_PRIMASK() == 1 && g_CriticalNesting == 1, "outer_enter_masks");
    OS_EnterCritical();
    test_check(__get_PRIMASK() == 1 && g_CriticalNesting == 2, "inner_enter_keeps_mask");
    OS_ExitCritical();
    test_check(__get_PRIMASK() == 1 && g_CriticalNesting == 1, "inner_exit_still_masks");
    OS_ExitCritical();
    test_check(__get_PRIMASK() == mask && g_CriticalNesting == 0, "outer_exit_restores_prior_mask");
    __set_PRIMASK(0);
}

/** @brief 重复交替原屏蔽状态，防止保存值滞留。 @return 不返回。 @note 不启动调度器，直接验证架构寄存器。 */
int main(void)
{
    test_begin("CRIT-01"); OS_Init();
    trial(1); trial(0); trial(1); trial(0);
    __set_BASEPRI(0x80);
    OS_EnterCritical(); OS_ExitCritical();
    test_check(__get_BASEPRI() == 0x80 && __get_PRIMASK() == 0, "basepri_unchanged");
    __set_BASEPRI(0);
    __set_FAULTMASK(1);
    OS_EnterCritical(); OS_ExitCritical();
    test_check(__get_FAULTMASK() == 1 && __get_PRIMASK() == 0, "faultmask_unchanged");
    __set_FAULTMASK(0);
    test_finish();
}

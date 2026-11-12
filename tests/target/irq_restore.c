/**
 * @file irq_restore.c
 * @brief CRIT-02：独立 IRQ 保护的 LIFO 恢复、真正异常嵌套和任务临界区延后 IRQ。
 * @note IRQ0 仅由 NVIC 软件挂起，不测试 UART 外设；无周期 tick，顺序可复现。
 */
#include "os_core.h"
#include "test_support.h"
extern volatile uint32_t g_CriticalNesting;
static volatile uint32_t irq_count, irq_checks, irq_context, svc_checks, svc_active, nested;

/** @brief 高优先级 IRQ 使用自己的两个保存值。 @return 无。 @note Handler 内只写观察值，避免嵌套 UART 输出影响窗口。 */
void UARTRX0_Handler(void)
{
    uint32_t nesting = g_CriticalNesting;
    uint32_t outer = OS_SaveIRQ();
    uint32_t inner = OS_SaveIRQ();
    irq_checks = (outer == 0 && inner == 1 && __get_PRIMASK() == 1);
    OS_RestoreIRQ(inner);
    irq_checks &= (__get_PRIMASK() == 1);
    OS_RestoreIRQ(outer);
    irq_checks &= (__get_PRIMASK() == 0 && g_CriticalNesting == nesting);
    irq_context = __get_IPSR();
    nested = svc_active;
    irq_count++;
}

/** @brief 在低优先级 SVC 中挂起高优先级 IRQ，逐层解除局部保护。 @return 无。 @note ISR 只使用局部保存值。 */
void SVC_Handler(void)
{
    svc_active = 1;
    uint32_t outer = OS_SaveIRQ();
    uint32_t inner = OS_SaveIRQ();
    NVIC_SetPendingIRQ(UARTRX0_IRQn);
    __DSB(); __ISB();
    svc_checks = (__get_IPSR() == 11 && outer == 0 && inner == 1 && irq_count == 0);
    OS_RestoreIRQ(inner);
    __DSB(); __ISB();
    svc_checks &= (__get_PRIMASK() == 1 && irq_count == 0);
    OS_RestoreIRQ(outer);
    __DSB(); __ISB();
    svc_checks &= (__get_PRIMASK() == 0 && irq_count == 1 && nested == 1 && g_CriticalNesting == 0);
    svc_active = 0;
}

/** @brief 在特权线程／真实 ISR 中检查独立保护，并核对任务临界区不会提前放 IRQ。 @return 不返回。 @note 不依赖 tick 或 ISR 调度功能。 */
int main(void)
{
    test_begin("CRIT-02"); OS_Init();
    uint32_t outer = OS_SaveIRQ(), inner = OS_SaveIRQ();
    test_check(outer == 0 && inner == 1 && __get_PRIMASK() == 1, "local_save_tokens");
    OS_RestoreIRQ(inner);
    test_check(__get_PRIMASK() == 1 && g_CriticalNesting == 0, "local_inner_restore_keeps_mask");
    OS_RestoreIRQ(outer);
    test_check(__get_PRIMASK() == 0 && g_CriticalNesting == 0, "local_outer_restore_opens");
    __disable_irq(); outer = OS_SaveIRQ(); OS_RestoreIRQ(outer);
    test_check(outer == 1 && __get_PRIMASK() == 1, "local_already_masked_stays_masked");
    __enable_irq();
    OS_EnterCritical(); outer = OS_SaveIRQ(); OS_RestoreIRQ(outer);
    test_check(outer == 1 && g_CriticalNesting == 1 && __get_PRIMASK() == 1,
               "local_guard_does_not_change_task_nesting");
    OS_ExitCritical();
    NVIC_SetPriority(SVCall_IRQn, 4); NVIC_SetPriority(UARTRX0_IRQn, 1);
    NVIC_ClearPendingIRQ(UARTRX0_IRQn); NVIC_EnableIRQ(UARTRX0_IRQn);
    __asm volatile("svc #0" ::: "memory");
    test_check(svc_checks && irq_count == 1 && nested == 1, "irq_preempts_svc_only_after_outer_restore");
    test_check(irq_checks && irq_context == 16 && g_CriticalNesting == 0,
               "real_nested_irq_has_independent_lifo_masks");
    OS_EnterCritical(); OS_EnterCritical();
    NVIC_SetPendingIRQ(UARTRX0_IRQn); __DSB(); __ISB();
    test_check(irq_count == 1, "task_outer_guard_defers_irq");
    OS_ExitCritical(); __DSB(); __ISB();
    test_check(irq_count == 1 && __get_PRIMASK() == 1, "task_inner_exit_defers_irq");
    OS_ExitCritical(); __DSB(); __ISB();
    test_check(irq_count == 2 && irq_checks && nested == 0 && g_CriticalNesting == 0,
               "task_outer_exit_delivers_irq_after_count_zero");
    NVIC_DisableIRQ(UARTRX0_IRQn);
    test_finish();
}

/**
 * @file neg_critical_isr.c
 * @brief CRITNEG-02：真实 SVC 不允许借用任务临界区计数。
 * @note 应触发原生断言，ISR 应使用 OS_SaveIRQ／OS_RestoreIRQ。
 */
#include "os_core.h"
#include "test_support.h"
/** @brief 故意从 Handler 误用线程接口。 @return 无。 @note 生产断言应阻止返回。 */
void SVC_Handler(void) { OS_EnterCritical(); }
/** @brief 触发真实异常。 @return 不返回。 @note 不用普通调用冒充 ISR。 */
int main(void)
{
    test_begin("CRITNEG-02"); OS_Init();
    __asm volatile("svc #0" ::: "memory");
    test_finish();
}

/**
 * @file sem_count.c
 * @brief SEM-01：初始化、消费、任务 post 和真实 SVC 中的计数边界。
 * @note 本用例不启动调度器；SVC 中只测试无等待者分支，不验证 ISR 竞争/调度。
 */
#include "os_core.h"
#include "test_support.h"
#include <stdint.h>
#include <string.h>

static OS_Sem semaphore;
static OS_Sem * volatile irq_sem;
static volatile OS_Status irq_result;
static volatile uint32_t irq_ipsr;
static uint8_t woken;

/** @brief 测试专用异常入口；记录调用确在 Handler 模式，避免把普通函数调用冒充 ISR。 */
void SVC_Handler(void)
{
    irq_ipsr = __get_IPSR();
    irq_result = OS_SemPostFromISR(irq_sem, &woken);
}

/**
 * @brief 通过真实 SVC 调用 ISR 接口，异常返回后读取状态。
 * @param sem 信号量指针，可为 NULL 以检查参数错误。
 * @return ISR 接口返回值；woken 保留作为待检查输出。
 */
static OS_Status post_in_exception(OS_Sem *sem)
{
    irq_sem = sem;
    __asm volatile("svc #0" ::: "memory");
    return irq_result;
}

/** @brief SEM-01 入口；每次边界设置独立，防止初始化错误掩盖溢出错误。 @return 不返回。 */
int main(void)
{
    test_begin("SEM-01");
    memset(&semaphore, 0xa5, sizeof(semaphore));
    test_check(OS_SemInit(&semaphore, 0) == OS_OK, "init_zero_status");
    test_check(semaphore.count == 0, "init_zero_overwrites_poison");
    test_check(semaphore.WaitList.Head == NULL && semaphore.WaitList.Tail == NULL,
               "init_clears_wait_list");

    memset(&semaphore, 0xa5, sizeof(semaphore));
    test_check(OS_SemInit(&semaphore, 3) == OS_OK, "init_three_status");
    test_check(semaphore.count == 3, "init_three_overwrites_poison");
    for (unsigned i = 0; i < 3; i++)
        test_check(OS_SemWait(&semaphore) == OS_OK, "consume_initial_permit");
    test_check(semaphore.count == 0, "three_consumptions_exhaust_initial_count");
    test_check(OS_SemInit(NULL, 0) == OS_ERR_PARAM, "init_null_rejected");
    test_check(OS_SemPost(NULL) == OS_ERR_PARAM, "task_post_null_rejected");

    test_check(OS_SemInit(&semaphore, UINT16_MAX) == OS_OK, "init_max_status");
    test_check(semaphore.count == UINT16_MAX, "init_accepts_max");
    /* 直接设置边界是测试夹具，不是修复：让 post 用例独立于初始化的成败。 */
    semaphore.count = UINT16_MAX - 1U;
    test_check(OS_SemPost(&semaphore) == OS_OK && semaphore.count == UINT16_MAX,
               "task_post_reaches_max");
    test_check(OS_SemPost(&semaphore) == OS_ERR_OVERFLOW, "task_post_overflow_error");
    test_check(semaphore.count == UINT16_MAX, "task_overflow_does_not_wrap");
    test_check(OS_SemPost(&semaphore) == OS_ERR_OVERFLOW && semaphore.count == UINT16_MAX,
               "task_repeated_overflow_unchanged");

    semaphore.count = UINT16_MAX - 1U;
    woken = FALSE;
    test_check(post_in_exception(&semaphore) == OS_OK && semaphore.count == UINT16_MAX,
               "isr_post_reaches_max");
    test_check(irq_ipsr == 11U, "isr_call_runs_in_svcall");
    test_check(woken == FALSE, "no_waiter_not_woken");
    woken = TRUE; /* 溢出失败不应覆盖调用者已有输出。 */
    test_check(post_in_exception(&semaphore) == OS_ERR_OVERFLOW, "isr_post_overflow_error");
    test_check(semaphore.count == UINT16_MAX, "isr_overflow_does_not_wrap");
    test_check(woken == TRUE, "isr_overflow_preserves_output");
    test_check(post_in_exception(&semaphore) == OS_ERR_OVERFLOW && semaphore.count == UINT16_MAX,
               "isr_repeated_overflow_unchanged");
    test_check(post_in_exception(NULL) == OS_ERR_PARAM && woken == TRUE,
               "isr_null_preserves_output");
    test_check(OS_SemPostFromISR(NULL, NULL) == OS_ERR_PARAM, "isr_null_without_output");
    semaphore.count = 0;
    test_check(OS_SemPostFromISR(&semaphore, NULL) == OS_OK && semaphore.count == 1,
               "optional_output_accepts_null");
    test_check(semaphore.WaitList.Head == NULL && semaphore.WaitList.Tail == NULL,
               "boundary_paths_leave_wait_list_empty");
    test_finish();
}

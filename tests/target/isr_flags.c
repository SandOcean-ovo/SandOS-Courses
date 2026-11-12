/**
 * @file isr_flags.c
 * @brief ISR-01：真实 SVC 中的累计标记、错误保持和原屏蔽恢复。
 * @note 不启动任务，仅隔离没有等待者的资源路径；真实唤醒由 ISR-02 覆盖。
 */
#include "os_core.h"
#include "test_support.h"
extern volatile uint32_t g_CriticalNesting;
static OS_Sem sem;
static OS_Queue queue;
static uint32_t buffer, output;
static volatile uint32_t mask_mode, actual_ipsr;

/** @brief 复用 TRUE 标记逐一检查成功和失败，不允许任何一次调用清零。 @return 无。 @note 原已屏蔽场景在 Handler 内设置，不能从屏蔽线程发 SVC。 */
void SVC_Handler(void)
{
    uint8_t woken = TRUE;
    uint32_t msg = 0x11223344;
    uint32_t saved = __get_PRIMASK();
    if (mask_mode) __disable_irq();
    actual_ipsr = __get_IPSR();
    woken = TRUE; /* 每项独立预填，避免前一个失败导致连锁假象。 */
    test_check(OS_SemPostFromISR(&sem, &woken) == OS_OK && woken == TRUE,
               "sem_success_preserves_true");
    woken = TRUE; /* 每项独立预填，避免前一个失败导致连锁假象。 */
    test_check(OS_QueueSendFromISR(&queue, &msg, &woken) == OS_OK && woken == TRUE,
               "queue_send_success_preserves_true");
    woken = TRUE; /* 每项独立预填，避免前一个失败导致连锁假象。 */
    test_check(OS_QueueSendFromISR(&queue, &msg, &woken) == OS_ERR_Q_FULL && woken == TRUE,
               "queue_full_preserves_true");
    woken = TRUE; /* 每项独立预填，避免前一个失败导致连锁假象。 */
    test_check(OS_QueueReceiveFromISR(&queue, &output, &woken) == OS_OK &&
               output == msg && woken == TRUE, "queue_receive_preserves_true");
    output = 0xDEADBEEF;
    woken = TRUE; /* 每项独立预填，避免前一个失败导致连锁假象。 */
    test_check(OS_QueueReceiveFromISR(&queue, &output, &woken) == OS_ERR_RESOURCE &&
               output == 0xDEADBEEF && woken == TRUE, "queue_empty_preserves_output_and_flag");
    sem.count = UINT16_MAX; /* 无并发对象的测试边界注入；不是生产调用方式。 */
    woken = TRUE; /* 每项独立预填，避免前一个失败导致连锁假象。 */
    test_check(OS_SemPostFromISR(&sem, &woken) == OS_ERR_OVERFLOW &&
               sem.count == UINT16_MAX && woken == TRUE, "sem_overflow_preserves_true");
    woken = TRUE; /* 每项独立预填，避免前一个失败导致连锁假象。 */
    test_check(OS_SemPostFromISR(NULL, &woken) == OS_ERR_PARAM &&
               OS_QueueSendFromISR(NULL, &msg, &woken) == OS_ERR_PARAM &&
               OS_QueueReceiveFromISR(NULL, &output, &woken) == OS_ERR_PARAM && woken == TRUE,
               "null_objects_preserve_flag");
    test_check(__get_PRIMASK() == mask_mode && g_CriticalNesting == 0,
               "all_paths_restore_mask_without_task_nesting");
    woken = FALSE; sem.count = 0;
    test_check(OS_SemPostFromISR(&sem, &woken) == OS_OK && woken == FALSE,
               "no_waiter_keeps_false");
    test_check(OS_QueueSendFromISR(&queue, &msg, NULL) == OS_OK &&
               OS_QueueReceiveFromISR(&queue, &output, NULL) == OS_OK,
               "null_flag_is_optional");
    __set_PRIMASK(saved);
}

/** @brief 在原开／关两种屏蔽场景调用真实 SVC。 @return 不返回。 @note 最小复现兼容修复前内核。 */
int main(void)
{
    test_begin("ISR-01"); OS_Init();
    for (mask_mode = 0; mask_mode < 2; mask_mode++) {
        OS_SemInit(&sem, 0); OS_QueueInit(&queue, &buffer, sizeof(buffer), 1);
        __asm volatile("svc #0" ::: "memory");
        test_check(actual_ipsr == 11 && __get_PRIMASK() == 0, "real_svc_returns_to_unmasked_thread");
    }
    test_finish();
}

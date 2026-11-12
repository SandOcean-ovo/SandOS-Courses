/**
 * @file blocking_context_legal.c
 * @brief CTX-05：合法线程的队列／内存池阻塞和醒后内部临界区不被误拒绝。
 * @note 关闭 tick，提供者逐个释放资源；高优先级消费者必须在提供者返回前完成操作。
 */
#include "os_core.h"
#include "test_support.h"

static OS_Queue queue;
static OS_Mem pool;
static uint32_t queue_data, pool_data[4];
static OS_TCB consumer, provider;
static uint32_t consumer_stack[256], provider_stack[256];
static void *held_block;
static volatile unsigned progress;

/** @brief 依次阻塞于空队列和耗尽池，再归还块。 @param unused 未使用。 @return 不返回。 @note 入口检查只针对调用者，不检查内部重入临界区。 */
static void task_consumer(void *unused)
{
    uint32_t message = 0;
    (void)unused;
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    held_block = OS_MemGet(&pool);
    test_check(held_block == pool_data, "legal_memory_fast_path");
    test_check(OS_QueueReceive(&queue, &message) == OS_OK && message == 0xCAFEBABEU,
               "legal_queue_receive_after_block");
    progress = 1;
    void *block = OS_MemGet(&pool);
    test_check(block == held_block, "legal_memory_get_after_block");
    test_check(OS_MemPut(&pool, block) == OS_OK, "legal_return_block");
    progress = 2;
    for (;;) OS_Delay(100);
}

/** @brief 检查阻塞队列并供应资源，确认真实抢占与等待节点清理。 @param unused 未使用。 @return 不返回。 @note 不依赖 tick 到期或随机延时。 */
static void task_provider(void *unused)
{
    uint32_t message = 0xCAFEBABEU;
    (void)unused;
    test_check(queue.WaitReadList.Head == &consumer && consumer.State == TASK_BLOCKED,
               "consumer_waits_on_empty_queue");
    test_check(OS_QueueSend(&queue, &message) == OS_OK && progress == 1 &&
               queue.WaitReadList.Head == NULL && pool.WaitList.Head == &consumer &&
               consumer.State == TASK_BLOCKED,
               "queue_wakeup_preempts_then_memory_blocks");
    test_check(OS_MemPut(&pool, held_block) == OS_OK && progress == 2 &&
               pool.WaitList.Head == NULL && pool.WaitList.Tail == NULL &&
               pool.FreeBlocks == 1 && pool.FreeList == pool_data,
               "memory_wakeup_preempts_and_restores_pool");
    test_check(queue.MsgCount == 0 && queue.WaitReadList.Tail == NULL &&
               ReadyList[1].Head == NULL && CurrentTCB == &provider &&
               __get_PRIMASK() == 0, "legal_internal_critical_sections_complete");
    test_finish();
}

/** @brief 建立单槽队列、单块池和两个静态任务。 @return 不返回。 @note 资源初始化在启动前完成。 */
int main(void)
{
    test_begin("CTX-05"); OS_Init();
    OS_QueueInit(&queue, &queue_data, sizeof(queue_data), 1);
    test_check(OS_MemInit(&pool, pool_data, 1, sizeof(pool_data)) == OS_OK, "init_pool");
    test_check(OS_TaskCreate(&consumer, task_consumer, NULL, consumer_stack, 256, 1) == OS_OK,
               "create_consumer");
    test_check(OS_TaskCreate(&provider, task_provider, NULL, provider_stack, 256, 3) == OS_OK,
               "create_provider");
    OS_StartScheduler();
}

/**
 * @file blocking_context.c
 * @brief CTX-01：真实异常／调用者屏蔽状态中，阻塞 API 必须在快路径之前拒绝。
 * @note 先检查资源可用场景，旧实现会消耗资源而非卡在空队列；发现污染立即结束。
 */
#include "os_core.h"
#include "test_support.h"
#include <stdio.h>

/* 预先锁定的新错误码，允许本复现程序仍与修复前的头文件一起编译。 */
enum { EXPECT_CRITICAL = 21 };
extern volatile uint32_t g_CriticalNesting; /* 仅测试观察内部计数，不新增公开 API。 */
static OS_TCB runner;
static uint32_t stack_runner[512];
static OS_Sem sem;
static OS_Mutex mutex;
static OS_Queue queue;
static OS_Mem pool;
static uint32_t queue_data, pool_data[4], output;
static volatile unsigned mode, available, trial_intact;
static volatile uint32_t actual_ipsr;

/** @brief 保存本试验可触及的全部对象、队列及数据，填零结构 padding 后再比较。 */
struct Snapshot {
    OS_TCB task;
    OS_TCB *current, *next;
    OS_List ready[OS_MAX_PRIO], delay;
    OS_Sem semaphore;
    OS_Mutex lock;
    OS_Queue messages;
    OS_Mem memory;
    uint32_t bitmap, nesting, primask, basepri, faultmask, pending;
    uint32_t msg, blocks[4], destination;
};
static struct Snapshot before, after;

/** @brief 只读复制被测状态。 @param s 快照目标。 @return 无。 @note 不读取有清除副作用的 SysTick CTRL。 */
static void snapshot(struct Snapshot *s)
{
    memset(s, 0, sizeof(*s));
    memcpy(&s->task, &runner, sizeof(runner));
    s->current = CurrentTCB; s->next = NextTCB;
    memcpy(s->ready, ReadyList, sizeof(ReadyList)); s->delay = DelayList;
    memcpy(&s->semaphore, &sem, sizeof(sem));
    memcpy(&s->lock, &mutex, sizeof(mutex));
    memcpy(&s->messages, &queue, sizeof(queue));
    memcpy(&s->memory, &pool, sizeof(pool));
    s->bitmap = g_PrioMap; s->nesting = g_CriticalNesting;
    s->primask = __get_PRIMASK(); s->basepri = __get_BASEPRI(); s->faultmask = __get_FAULTMASK();
    s->pending = SCB->ICSR & (SCB_ICSR_PENDSVSET_Msk | SCB_ICSR_PENDSTSET_Msk);
    s->msg = queue_data; s->destination = output;
    memcpy(s->blocks, pool_data, sizeof(pool_data));
}

/** @brief 在已建立的非法上下文中尝试四种资源获取。 @return 无。 @note 不调用 void Delay，断言另由 GDB 观察。 */
static void attempt(void)
{
    actual_ipsr = __get_IPSR();
    OS_Status expected = (mode == 1 || mode == 3) ? OS_ERR_ISR : (OS_Status)EXPECT_CRITICAL;
    snapshot(&before);
    OS_Status sem_result = OS_SemWait(&sem);
    OS_Status mutex_result = OS_MutexPend(&mutex);
    OS_Status queue_result = OS_QueueReceive(&queue, &output);
    void *mem_result = OS_MemGet(&pool);
    snapshot(&after);
    /* 先收集再打印，UART 不参与两个快照之间的状态窗口。 */
    printf("CONTEXT mode=%u available=%u ipsr=%lu\n", mode, available, (unsigned long)actual_ipsr);
    test_check(sem_result == expected, "semaphore_context_error");
    test_check(mutex_result == expected, "mutex_context_error");
    test_check(queue_result == expected, "queue_context_error");
    test_check(mem_result == NULL, "memory_context_returns_null");
    test_check(actual_ipsr == ((mode == 1 || mode == 3) ? 11U : 0U), "actual_exception_number");
    trial_intact = memcmp(&before, &after, sizeof(before)) == 0;
    test_check(trial_intact, "rejection_preserves_all_state_and_masks");
}

/** @brief 真实 SVC 覆盖异常与异常内 PRIMASK 屏蔽组合。 @return 无。 @note 不能在已屏蔽的线程里发 SVC。 */
void SVC_Handler(void)
{
    uint32_t saved = __get_PRIMASK();
    if (mode == 3) __disable_irq();
    attempt();
    __set_PRIMASK(saved);
}

/** @brief 配置独立资源快照，资源不足组也必须立即拒绝而非阻塞。 @return 无。 @note 在合法线程中准备对象。 */
static void prepare(void)
{
    OS_SemInit(&sem, available ? 1 : 0);
    OS_MutexInit(&mutex);
    queue_data = 0x11223344U; output = 0xA5A5A5A5U;
    OS_QueueInit(&queue, &queue_data, sizeof(queue_data), 1);
    if (available) {
        uint32_t message = 0xDEADBEEFU;
        OS_QueueSend(&queue, &message);
    }
    OS_MemInit(&pool, pool_data, 1, sizeof(pool_data));
    if (!available) (void)OS_MemGet(&pool);
}

/** @brief 依次验证六种上下文与两种资源状态。 @param unused 未使用。 @return 不返回。 @note tick 关闭，试验顺序完全由代码控制。 */
static void task_runner(void *unused)
{
    (void)unused;
    SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    for (mode = 0; mode < 6; ++mode) {
        for (available = 1; ; available = 0) {
            prepare();
            if (mode == 0) {
                OS_EnterCritical(); OS_EnterCritical();
                attempt();
                OS_ExitCritical(); OS_ExitCritical();
            } else if (mode == 1 || mode == 3) {
                __asm volatile("svc #0" ::: "memory");
            } else if (mode == 2) {
                uint32_t saved = __get_PRIMASK(); __disable_irq(); attempt(); __set_PRIMASK(saved);
            } else if (mode == 4) {
                uint32_t saved = __get_BASEPRI(); __set_BASEPRI(0xF0U); attempt(); __set_BASEPRI(saved);
            } else {
                uint32_t saved = __get_FAULTMASK(); __set_FAULTMASK(1); attempt(); __set_FAULTMASK(saved);
            }
            /* 旧实现先在可用资源组暴露污染；不要继续进入真正阻塞路径掩盖原因。 */
            if (!trial_intact) test_finish();
            if (!available) break;
        }
    }
    test_check(OS_SemWait(NULL) == OS_ERR_PARAM && OS_MutexPend(NULL) == OS_ERR_PARAM &&
               OS_QueueReceive(NULL, &output) == OS_ERR_PARAM && OS_MemGet(NULL) == NULL,
               "null_arguments_keep_existing_errors");
    test_finish();
}

/** @brief 建立真实线程再测试上下文，保留现有启动协议。 @return 不返回。 @note 不以主机桩冒充异常状态。 */
int main(void)
{
    test_begin("CTX-01"); OS_Init();
    test_check(OS_TaskCreate(&runner, task_runner, NULL, stack_runner, 512, 2) == OS_OK,
               "create_context_runner");
    OS_StartScheduler();
}

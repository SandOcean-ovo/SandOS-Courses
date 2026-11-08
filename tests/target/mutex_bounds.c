/**
 * @file mutex_bounds.c
 * @brief MUTEX-02：同锁递归的 255/256 边界、非持有者及第二把不同锁拒绝。
 * @note 停止 tick；使用高优先级见证任务测试被占用第二把锁，拒绝必须立即返回。
 */
#include "os_core.h"
#include "test_support.h"
static OS_Mutex mutex, other;
static OS_Sem gate;
static OS_TCB owner, witness;
static uint32_t stack_owner[256], stack_witness[256];
static volatile unsigned witness_done;

/** @brief 见证任务不拥有 mutex，拥有 other 后不得再阻塞于 mutex。 @param unused 未使用。 @return 不返回。 @note 顺序由门闩／阻塞确定，tick 已停或由最高优先级任务先停止。 */
static void task_witness(void *unused)
{
    (void)unused;
    SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    OS_SemWait(&gate);
    test_check(OS_MutexPost(&mutex) == OS_ERR_NOT_OWNER && mutex.Owner == &owner &&
               mutex.NestCount == 1, "non_owner_cannot_release");
    test_check(OS_MutexPend(&other) == OS_OK, "witness_acquires_its_first_lock");
    test_check(OS_MutexPend(&mutex) == OS_ERR_RESOURCE, "busy_second_lock_rejected");
    test_check(mutex.Owner == &owner && mutex.WaitList.Head == NULL &&
               owner.Priority == 1 && other.Owner == &witness && other.NestCount == 1,
               "busy_second_lock_rejection_has_no_inheritance_or_queue_effect");
    test_check(OS_MutexPost(&other) == OS_OK, "witness_releases_its_lock");
    witness_done = 1;
    for (;;) OS_Delay(100);
}

/** @brief 持有者验证所有递归边界，并释放后重新获取另一把锁。 @param unused 未使用。 @return 不返回。 @note 顺序由门闩／阻塞确定，tick 已停或由最高优先级任务先停止。 */
static void task_owner(void *unused)
{
    (void)unused;
    test_check(OS_MutexPend(NULL) == OS_ERR_PARAM && OS_MutexPost(NULL) == OS_ERR_PARAM,
               "null_lock_arguments");
    test_check(OS_MutexPend(&mutex) == OS_OK, "first_depth_is_one");
    unsigned ok = 0;
    for (unsigned i = 1; i < UINT8_MAX; ++i) ok += OS_MutexPend(&mutex) == OS_OK;
    test_check(ok == UINT8_MAX - 1 && mutex.NestCount == UINT8_MAX && mutex.Owner == &owner,
               "recursive_depth_reaches_255");
    test_check(OS_MutexPend(&mutex) == OS_ERR_NESTING, "depth_256_rejected");
    test_check(mutex.NestCount == UINT8_MAX && mutex.Owner == &owner &&
               mutex.WaitList.Head == NULL, "overflow_rejection_preserves_lock");
    if (mutex.NestCount != UINT8_MAX) test_finish();
    ok = 0;
    for (unsigned i = 1; i < UINT8_MAX; ++i) ok += OS_MutexPost(&mutex) == OS_OK;
    test_check(ok == UINT8_MAX - 1 && mutex.NestCount == 1 && mutex.Owner == &owner,
               "partial_unlock_keeps_ownership");
    OS_SemPost(&gate);
    test_check(witness_done == 1, "busy_second_lock_returns_without_blocking");
    test_check(OS_MutexPend(&other) == OS_ERR_RESOURCE, "free_second_lock_rejected");
    test_check(other.Owner == NULL && other.NestCount == 0 && mutex.Owner == &owner &&
               mutex.NestCount == 1 && owner.Priority == 1, "free_second_rejection_preserves_both");
    test_check(OS_MutexPost(&mutex) == OS_OK && mutex.Owner == NULL && mutex.NestCount == 0,
               "final_unlock_clears_owner");
    test_check(OS_MutexPost(&mutex) == OS_ERR_NOT_OWNER, "unlocked_object_cannot_be_posted");
    test_check(OS_MutexPend(&other) == OS_OK && other.Owner == &owner,
               "different_lock_allowed_after_final_release");
    test_check(OS_MutexPost(&other) == OS_OK, "release_reacquired_lock");
    test_check(ReadyList[0].Head == NULL && ReadyList[1].Head == &owner &&
               ReadyList[1].Tail == &owner && owner.Prev == NULL && owner.Next == NULL &&
               g_PrioMap == ((1U << 1) | (1U << (OS_MAX_PRIO - 1))),
               "boundary_tests_preserve_ready_lists");
    test_finish();
}

/** @brief 用非零内存检查初始化后，启动持有者和见证任务。 @return 不返回。 @note 顺序由门闩／阻塞确定，tick 已停或由最高优先级任务先停止。 */
int main(void)
{
    test_begin("MUTEX-02"); OS_Init();
    memset(&mutex, 0xA5, sizeof(mutex));
    test_check(OS_MutexInit(NULL) == OS_ERR_PARAM, "init_null_rejected");
    test_check(OS_MutexInit(&mutex) == OS_OK && mutex.Owner == NULL && mutex.NestCount == 0 &&
               mutex.WaitList.Head == NULL && mutex.WaitList.Tail == NULL, "init_poisoned_mutex");
    OS_MutexInit(&other); OS_SemInit(&gate, 0);
    memset(&owner, 0xA5, sizeof(owner));
    test_check(OS_TaskCreate(&owner, task_owner, NULL, stack_owner, 256, 1) == OS_OK, "create_owner");
    test_check(OS_TaskCreate(&witness, task_witness, NULL, stack_witness, 256, 0) == OS_OK, "create_witness");
    test_check(owner.HeldMutex == NULL && witness.HeldMutex == NULL, "task_create_clears_ownership");
    OS_StartScheduler();
}

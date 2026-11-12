/**
 ******************************************************************************
 * @file    os_core.c
 * @author  SandOcean
 * @version V1.0
 * @date    2025-12-14
 * @brief   RTOS 内核核心逻辑实现
 *
 * 本文件包含RTOS 的独立于硬件的逻辑实现：
 * - OS 初始化与启动逻辑 (OS_Init, OS_Start)
 * - 任务调度器算法 (Scheduler) 实现
 * - SysTick 时钟节拍处理 (Timebase management)
 * - 阻塞延时处理 (osDelay) 与就绪表管理
 *
 ******************************************************************************
 */

#include "os_core.h"
#include "benchmark.h"

#include <stdio.h>

/* 变量定义 ------------------------------------------------------ */

volatile uint32_t g_SystemTickCount = 0; // 系统心跳计数器

volatile uint32_t g_CriticalNesting = 0; // 仅任务临界区；ISR 使用局部保存值。
/* 只在最外层进入时写入；单核上禁止在任务临界区内阻塞或切换任务。 */
static uint32_t g_CriticalSavedMask = 0;

volatile uint32_t g_PrioMap = 0; // 任务位图

volatile uint8_t g_OSRunning = OS_SCHED_STOPPED; // 三态；仅 RUNNING 表示可调度。

OS_List ReadyList[OS_MAX_PRIO]; // 任务就绪链表
OS_List DelayList;              // 有序延时链表

OS_TCB *CurrentTCB = NULL;
OS_TCB *NextTCB = NULL;

OS_TCB IdleTaskTCB;
uint32_t IdleTaskStack[IDLE_STACK_SIZE];

/* 私有函数定义 ------------------------------------------------------ */
void OS_CheckStackOverflow(void)
{
    if (CurrentTCB == NULL)
        return;

    if (*(CurrentTCB->stackLimit) != OS_STACK_MAGIC_VAL || CurrentTCB->stackPtr <= CurrentTCB->stackLimit)
    {
        OS_Disable_IRQ();
        OS_ASSERT(0);
    }
}

OS_TCB *FindNextTask(void)
{
    OS_ASSERT(g_PrioMap != 0);

    uint8_t TopPrio = OS_GetTopPrio(g_PrioMap);

    OS_TCB *next_task = ReadyList[TopPrio].Head;
    OS_ASSERT(next_task != NULL);
    return next_task;
}

void IdleTask(void *param)
{
    for (;;)
        ;
}

void List_Init(OS_List *list)
{
    OS_ASSERT(list != NULL);
    list->Head = NULL;
    list->Tail = NULL;
}

void List_InsertTail(OS_List *list, OS_TCB *tcb)
{
    OS_ASSERT(list != NULL && tcb != NULL);
    tcb->Next = NULL;
    if (list->Head == NULL) // 如果之前链表是空的
    {
        list->Head = tcb;
        list->Tail = tcb; // 链表头尾都是tcb
        tcb->Prev = NULL; // tcb前面没东西
    }
    else // 如果之前链表有东西
    {
        list->Tail->Next = tcb;
        tcb->Prev = list->Tail;
        list->Tail = tcb;
    }
}

void List_Remove(OS_List *list, OS_TCB *tcb)
{
    OS_ASSERT(list != NULL && tcb != NULL);
    if (tcb->Prev == NULL) // 处理前向节点
    {
        list->Head = tcb->Next;
    }
    else
    {
        tcb->Prev->Next = tcb->Next;
    }
    if (tcb->Next == NULL)
    {
        list->Tail = tcb->Prev;
    }
    else
    {
        tcb->Next->Prev = tcb->Prev;
    }

    tcb->Prev = NULL;
    tcb->Next = NULL;
}

OS_TCB *List_PopHead(OS_List *list)
{
    OS_ASSERT(list != NULL);
    OS_TCB *head = list->Head;
    if (list->Head != NULL)
        List_Remove(list, head);
    return head;
}

void OS_DelayListInsert(OS_List *list, OS_TCB *tcb, uint32_t ticks)
{
    OS_TCB *iter;

    OS_ASSERT(list != NULL && tcb != NULL);

    if (list->Head == NULL)
    {
        tcb->DelayTicks = ticks;
        List_InsertTail(list, tcb);
        return;
    }

    iter = list->Head;
    while (iter != NULL && ticks >= iter->DelayTicks)
    {
        ticks -= iter->DelayTicks;
        iter = iter->Next;
    }

    tcb->DelayTicks = ticks;
    if (iter == NULL)
    {
        List_InsertTail(list, tcb);
    }
    else if (iter == list->Head)
    {
        iter->DelayTicks -= ticks;
        tcb->Next = iter;
        iter->Prev = tcb;
        tcb->Prev = NULL;
        list->Head = tcb;
    }
    else
    {
        iter->DelayTicks -= ticks;
        tcb->Next = iter;
        tcb->Prev = iter->Prev;
        iter->Prev->Next = tcb;
        iter->Prev = tcb;
    }
}

void OS_DelayListAdvance(OS_List *list)
{
    OS_ASSERT(list != NULL);

    if (list->Head != NULL && list->Head->DelayTicks > 0U)
    {
        list->Head->DelayTicks--;
    }
}

void OS_ReadyListAdd(OS_TCB *tcb)
{
    OS_ASSERT(tcb != NULL);
    g_PrioMap |= (1U << tcb->Priority);
    List_InsertTail(&ReadyList[tcb->Priority], tcb);
}

void OS_ReadyListRemove(OS_TCB *tcb)
{
    OS_ASSERT(tcb != NULL);
    List_Remove(&ReadyList[tcb->Priority], tcb);
    if (ReadyList[tcb->Priority].Head == NULL)
        g_PrioMap &= ~(1U << tcb->Priority);
}

void OS_TaskSuspend(OS_List *p_wait_list)
{
    OS_ASSERT(p_wait_list != NULL);

    if (g_OSRunning != OS_SCHED_RUNNING)
        return;

    CurrentTCB->State = TASK_BLOCKED;
    OS_ReadyListRemove(CurrentTCB);
    List_InsertTail(p_wait_list, CurrentTCB);

    NextTCB = FindNextTask();
    OS_Schedule();
}

OS_TCB *OS_TaskResume(OS_List *p_wait_list)
{
    OS_ASSERT(p_wait_list != NULL);

    if (p_wait_list->Head == NULL)
        return NULL;

    OS_TCB *TaskToWake = List_PopHead(p_wait_list);
    TaskToWake->State = TASK_READY;
    OS_ReadyListAdd(TaskToWake);

    return TaskToWake;
}

void OS_TaskResumeAndSchedule(OS_List *p_wait_list)
{
    if (OS_TaskResume(p_wait_list) != NULL)
    {
        NextTCB = FindNextTask();
        OS_Schedule();
    }
}

/**
 * @brief ISR 尾部根据最新全部就绪状态请求切换，不在 Handler 内直接切换任务。
 * @param higher_prio_woken 当前 ISR 的累计唤醒标记，非零表示需要重选。
 * @return 无；线程误用断言；FALSE 或系统不可调度时无操作。
 * @note 嵌套 ISR 可各自调用。重选与挂起 PendSV 在同一保护内，不能用旧 NextTCB。
 */
void OS_YieldFromISR(uint8_t higher_prio_woken)
{
    if (OS_GetExceptionNumber() == 0U)
    {
        OS_ASSERT(0);
        return;
    }
    if (higher_prio_woken == FALSE)
        return;
    uint32_t saved_mask = OS_SaveIRQ();
    if (g_OSRunning == OS_SCHED_RUNNING && CurrentTCB != NULL)
    {
        NextTCB = FindNextTask();
        if (NextTCB != CurrentTCB)
            OS_Schedule();
    }
    OS_RestoreIRQ(saved_mask);
}

/* 函数实现 ----------------------------------------------------------- */

/**
 * @brief 建立静态任务的初始栈与就绪节点，同时清空单锁所有权。
 * @param tcb 由调用者提供的任务对象。
 * @param task_function 任务入口。
 * @param task_param 传给任务入口的参数。
 * @param stack_init_address 栈缓冲区起点。
 * @param stack_depth 栈元素数；本轮不新增容量或对齐边界检查。
 * @param priority 静态基础优先级，数值越小优先级越高。
 * @return OS_OK；空对象／入口／栈或非法优先级返回 OS_ERR_PARAM。
 * @note 必须在启动前创建；运行期拒绝与栈大小专项仍待后续修复。
 *       HeldMutex 位于 TCB 末尾，首字段 stackPtr 的汇编约定保持不变。
 */
OS_Status OS_TaskCreate(OS_TCB *tcb, OS_TaskFunc_t task_function, void *task_param, uint32_t *stack_init_address, uint32_t stack_depth, uint8_t priority)
{
    if (tcb == NULL || task_function == NULL || stack_init_address == NULL || priority > OS_MAX_PRIO - 1)
        return OS_ERR_PARAM;
    tcb->stackPtr = OS_StackInit(task_function, task_param, stack_init_address, stack_depth);

    tcb->stackLimit = stack_init_address;
    *(tcb->stackLimit) = OS_STACK_MAGIC_VAL;

    tcb->DelayTicks = 0;
    tcb->State = TASK_READY;
    tcb->Priority = priority;
    tcb->OriginalPrio = priority;
    /* 任务对象可能来自未清零内存；单锁限制必须从确定的空所有权开始。 */
    tcb->HeldMutex = NULL;

    OS_ReadyListAdd(tcb);
    return OS_OK;
}

/**
 * @brief 在无并发、未启动阶段建立内核初始链表与 idle，清空调度指针和状态。
 * @return 无。
 * @note 不提供运行中重置语义；必须在任何应用任务创建之前调用。
 */
void OS_Init(void)
{
    // 1. 初始化全局变量
    g_OSRunning = OS_SCHED_STOPPED;
    CurrentTCB = NULL;
    NextTCB = NULL;
    g_SystemTickCount = 0;
    g_CriticalNesting = 0;
    g_CriticalSavedMask = 0;
    g_PrioMap = 0; // 清空位图

    // 2. 初始化就绪链表
    for (int i = 0; i < OS_MAX_PRIO; i++)
    {
        List_Init(&ReadyList[i]);
    }

    // 3. 初始化延时链表
    List_Init(&DelayList);

    // 4. 创建空闲任务
    OS_TaskCreate(&IdleTaskTCB, IdleTask, NULL, IdleTaskStack, IDLE_STACK_SIZE, OS_MAX_PRIO - 1);
}

/**
 * @brief 从未启动状态准备首次 PendSV；直到上下文恢复完成才允许 tick 调度。
 * @return 正常不返回；重复启动、非法上下文或时钟配置失败先断言，再防御返回。
 * @note Timer 不得自行开中断。准备期间使用局部保护，不修改任务临界区计数。
 */
void OS_StartScheduler(void)
{
    if (g_OSRunning != OS_SCHED_STOPPED || OS_GetExceptionNumber() != 0U ||
        g_CriticalNesting != 0U || OS_IsSchedulingMasked() != 0U)
    {
        OS_ASSERT(0);
        return;
    }
    uint32_t saved_mask = OS_SaveIRQ();
    g_OSRunning = OS_SCHED_STARTING;
    NextTCB = FindNextTask();
    CurrentTCB = NULL; /* 首次 PendSV 不保存尚不存在的任务上下文。 */
    if (OS_Init_Timer(1) != 0U)
    {
        /* 时钟／周期配置未写硬件；恢复为未启动，不能带失败配置触发首次 PendSV。 */
        g_OSRunning = OS_SCHED_STOPPED;
        CurrentTCB = NULL;
        NextTCB = NULL;
        OS_RestoreIRQ(saved_mask);
        OS_ASSERT(0);
        return;
    }
    OS_Schedule();
    /* SysTick 比 PendSV 优先级高；即使二者同时 pending，STARTING 的 tick 也只忽略。
     * RUNNING 由 PendSV 在 PSP／寄存器／异常返回方式全部就绪后发布。 */
    OS_RestoreIRQ(saved_mask);
    for (;;)
        ;
}

/**
 * @brief SysTick 推进时间、唤醒到期任务并选择下一就绪任务。
 * @return 无；STOPPED／STARTING 时无操作，不累计启动前 tick。
 * @note 仅 RUNNING 允许访问 CurrentTCB；与资源 ISR 共用独立 PRIMASK 保护，不借任务计数。
 */
void OS_Tick_Handler(void)
{
#if OS_BENCHMARK_ENABLE
    uint8_t benchmark_tick_active;
    uint32_t benchmark_tick_start = 0U;
#endif

    if (g_OSRunning != OS_SCHED_RUNNING)
        return;

#if OS_BENCHMARK_ENABLE
    benchmark_tick_active = g_BenchmarkRuntimeActive;
    if (benchmark_tick_active != FALSE)
    {
        benchmark_tick_start = DWT->CYCCNT;
    }
#endif

    // 1. 安全检查
    OS_ASSERT(CurrentTCB != NULL);

    uint32_t saved_mask = OS_SaveIRQ(); /* 与高优先级资源 ISR 的链表更新串行。 */

    OS_CheckStackOverflow(); // 栈溢出检查

    // 2. 更新系统时间
    g_SystemTickCount++;

    OS_DelayListAdvance(&DelayList);
    while (DelayList.Head != NULL && DelayList.Head->DelayTicks == 0U)
    {
        OS_TCB *tcb_to_wake = List_PopHead(&DelayList);

        tcb_to_wake->State = TASK_READY;
        OS_ReadyListAdd(tcb_to_wake);
    }

    OS_List *ls = &ReadyList[CurrentTCB->Priority];

    if (CurrentTCB->State == TASK_READY && ls->Head != ls->Tail)
    {
        List_Remove(ls, CurrentTCB);
        List_InsertTail(ls, CurrentTCB);
    }

    // 4. 核心调度逻辑
    NextTCB = FindNextTask();

    if (NextTCB != CurrentTCB)
    {
        OS_Schedule();
    }

#if OS_BENCHMARK_ENABLE
    if (benchmark_tick_active != FALSE)
    {
        Benchmark_RuntimeRecordKernelTick(DWT->CYCCNT - benchmark_tick_start);
    }
#endif
    OS_RestoreIRQ(saved_mask);
}

/**
 * @brief 在阻塞／调度 API 自身进入临界区之前检查调用者上下文。
 * @return Handler 返回 OS_ERR_ISR；用户嵌套计数或架构屏蔽状态返回
 *         OS_ERR_CRITICAL；合法线程返回 OS_OK。
 * @note 仅查询不修复状态。ISR 优先于屏蔽错误，API 内部临界区不能再次调用它。
 */
static OS_Status OS_CheckBlockingContext(void)
{
    if (OS_GetExceptionNumber() != 0U)
        return OS_ERR_ISR;
    if (g_CriticalNesting != 0U || OS_IsSchedulingMasked() != 0U)
        return OS_ERR_CRITICAL;
    return OS_OK;
}

/**
 * @brief 合法线程延时或在本优先级让出；非法上下文断言发生于所有状态修改之前。
 * @param ticks 延时 tick 数，0 只让出同优先级执行机会。
 * @return 无；不改变原有 void 接口。
 * @note 不允许 ISR、用户外层临界区或调用者屏蔽调度；本次不修复屏蔽恢复机制。
 */
void OS_Delay(uint32_t ticks)
{
    OS_Status context = OS_CheckBlockingContext();
    if (context != OS_OK)
    {
        OS_ASSERT(context == OS_OK);
        return; /* 即使将来断言被配置为返回，也不能执行后续状态修改。 */
    }
    OS_EnterCritical();

    if (ticks == 0U)
    {
        /* 零延时只轮转本优先级的就绪队列，不等待未来的 tick 来解除阻塞。 */
        OS_List *ready = &ReadyList[CurrentTCB->Priority];
        if (ready->Head != ready->Tail)
        {
            List_Remove(ready, CurrentTCB);
            List_InsertTail(ready, CurrentTCB);
        }
        NextTCB = FindNextTask();
        if (NextTCB != CurrentTCB)
            OS_Schedule();
        OS_ExitCritical();
        return;
    }

    CurrentTCB->State = TASK_BLOCKED;
    OS_ReadyListRemove(CurrentTCB);

    OS_DelayListInsert(&DelayList, CurrentTCB, ticks);

    NextTCB = FindNextTask();

    OS_Schedule();

    OS_ExitCritical(); /* 修改成我们的进入退出临界区函数 */
}

/**
 * @brief 进入特权线程的嵌套临界区，只在最外层保存调用者的 PRIMASK。
 * @return 无；ISR 或计数溢出误用时断言。
 * @note 先屏蔽再修改计数，内层不得覆盖最外层原值；不修改 BASEPRI／FAULTMASK。
 */
void OS_EnterCritical(void)
{
    if (OS_GetExceptionNumber() != 0U)
    {
        OS_ASSERT(0); /* ISR 必须使用自己的保存值，不能借用任务计数。 */
        return;
    }
    uint32_t saved_mask = OS_SaveIRQ();
    if (g_CriticalNesting == UINT32_MAX)
    {
        OS_ASSERT(0);
        OS_RestoreIRQ(saved_mask);
        return;
    }
    if (g_CriticalNesting == 0U)
        g_CriticalSavedMask = saved_mask;
    g_CriticalNesting++;
}

/**
 * @brief 退出线程临界区；最后一层恢复最外层进入前的 PRIMASK。
 * @return 无；ISR 或未配对退出时断言。
 * @note 先更新计数再恢复屏蔽，恢复为 0 时 pending IRQ／PendSV 可能立即执行。
 */
void OS_ExitCritical(void)
{
    if (OS_GetExceptionNumber() != 0U || g_CriticalNesting == 0U)
    {
        OS_ASSERT(0);
        return;
    }
    g_CriticalNesting--;
    if (g_CriticalNesting == 0U)
        OS_RestoreIRQ(g_CriticalSavedMask);
}

/**
 * @brief 初始化尚未发布的信号量；不支持对仍有等待者的对象重新初始化。
 * @param p_sem 静态分配的信号量对象。
 * @param init_count 初始可消费次数，包含 0 和 UINT16_MAX。
 * @return OS_OK；p_sem 为 NULL 时 OS_ERR_PARAM。
 * @note 在线程/初始化上下文、没有并发访问时调用；必须覆盖旧内存中的计数。
 */
OS_Status OS_SemInit(OS_Sem *p_sem, uint16_t init_count)
{
    if (p_sem == NULL)
        return OS_ERR_PARAM;
    p_sem->count = init_count;
    List_Init(&p_sem->WaitList);
    return OS_OK;
}

/**
 * @brief 在线程中获取一次许可；不足时 FIFO 无限等待。
 * @param p_sem 已初始化且仍有效的信号量。
 * @return OS_OK；空指针 OS_ERR_PARAM；Handler 中 OS_ERR_ISR；外层临界区或屏蔽调度时 OS_ERR_CRITICAL。
 * @note 入口检查在内部临界区之前；即使许可可用，非法上下文也不能消费计数。
 */
OS_Status OS_SemWait(OS_Sem *p_sem)
{
    if (p_sem == NULL)
        return OS_ERR_PARAM;
    OS_Status context = OS_CheckBlockingContext();
    if (context != OS_OK)
        return context;

    OS_EnterCritical();
    if (p_sem->count > 0) // 原本就有信号量
    {
        p_sem->count--;
        OS_ExitCritical();
        return OS_OK; // 成功返回
    }
    // 原本没信号量，我睡觉去了，直到信号量来了

    OS_TaskSuspend(&p_sem->WaitList);
    OS_ExitCritical();

    return OS_OK;
}

/**
 * @brief 在线程中释放一次许可；等待者 FIFO 交接与计数累积互斥。
 * @param p_sem 已初始化且仍有效的信号量。
 * @return OS_OK；空指针 OS_ERR_PARAM；无等待者且计数已满 OS_ERR_OVERFLOW。
 * @note 满计数检查和加一在同一临界区内；失败不改动计数或队列。
 */
OS_Status OS_SemPost(OS_Sem *p_sem)
{
    if (p_sem == NULL)
        return OS_ERR_PARAM;
    OS_EnterCritical();
    if (p_sem->WaitList.Head == NULL)
    {
        if (p_sem->count == UINT16_MAX)
        {
            OS_ExitCritical();
            return OS_ERR_OVERFLOW;
        }
        p_sem->count++;
    }
    else
    {
        OS_TaskResumeAndSchedule(&p_sem->WaitList);
    }

    OS_ExitCritical();
    return OS_OK;
}

/**
 * @brief 在 ISR 保护区内累计更高优先级唤醒结果。
 * @param task 本次实际唤醒的任务，可为 NULL。
 * @param flag 调用者的累计标记，可为 NULL。
 * @return 无。
 * @note 只置 TRUE 不清零；启动前无 CurrentTCB 时不作任务比较。
 */
static void OS_RecordISRWakeup(OS_TCB *task, uint8_t *flag)
{
    if (flag != NULL && task != NULL && g_OSRunning == OS_SCHED_RUNNING && CurrentTCB != NULL &&
        task->Priority < CurrentTCB->Priority)
        *flag = TRUE;
}

/**
 * @brief ISR 释放许可：在独立保护中检查上限、迁移等待者并累计唤醒。
 * @param p_sem 已初始化信号量，不能并发重新初始化。
 * @param p_HigherPrioTaskWoken 可选局部累计标记；仅可能置 TRUE。
 * @return OS_OK、OS_ERR_PARAM 或 OS_ERR_OVERFLOW；失败不改变对象和标记。
 * @note 不直接调度；调用者在 ISR 尾部把累计标记传给 OS_YieldFromISR。
 */
OS_Status OS_SemPostFromISR(OS_Sem *p_sem, uint8_t *p_HigherPrioTaskWoken)
{
    if (p_sem == NULL)
        return OS_ERR_PARAM;
    uint32_t saved_mask = OS_SaveIRQ();
    if (p_sem->WaitList.Head == NULL)
    {
        if (p_sem->count == UINT16_MAX)
        {
            OS_RestoreIRQ(saved_mask);
            return OS_ERR_OVERFLOW;
        }
        p_sem->count++;
    }
    else
        OS_RecordISRWakeup(OS_TaskResume(&p_sem->WaitList), p_HigherPrioTaskWoken);
    OS_RestoreIRQ(saved_mask);
    return OS_OK;
}

/**
 * @brief 初始化尚未发布的静态互斥锁，清空所有权、深度与等待队列。
 * @param p_mutex 尚无并发访问或等待者的对象，不能重置使用中的锁。
 * @return OS_OK；空指针返回 OS_ERR_PARAM。
 * @note 没有运行期重新初始化语义；OriginalPrio 字段保留上游布局，不用于多锁恢复。
 */
OS_Status OS_MutexInit(OS_Mutex *p_mutex)
{
    if (p_mutex == NULL)
        return OS_ERR_PARAM;
    p_mutex->Owner = NULL;
    p_mutex->NestCount = 0;
    p_mutex->OriginalPrio = OS_MAX_PRIO - 1;
    List_Init(&p_mutex->WaitList);
    return OS_OK;
}

/**
 * @brief 取得第一把锁、递归取得同锁，或无限等待并进行单锁直接优先级继承。
 * @param p_mutex 已初始化且有效的互斥锁。
 * @return OS_OK；空指针 OS_ERR_PARAM；同锁 255 层时 OS_ERR_NESTING；
 *         已持其他锁时 OS_ERR_RESOURCE，拒绝时没有所有权、队列或优先级副作用。
 * @note 仅线程且无用户外层临界区时调用；同优先级 FIFO，高优先级先等待。
 *       ISR 或调用者屏蔽调度时拒绝；不支持有限超时、多锁或传递继承。
 */
OS_Status OS_MutexPend(OS_Mutex *p_mutex)
{
    if (p_mutex == NULL)
        return OS_ERR_PARAM;

    OS_Status context = OS_CheckBlockingContext();
    if (context != OS_OK)
        return context;

    OS_EnterCritical();

    /* M1 只跟踪一把锁：在任何取得、继承、入队之前拒绝第二把不同锁。 */
    if (CurrentTCB->HeldMutex != NULL && CurrentTCB->HeldMutex != p_mutex)
    {
        OS_ExitCritical();
        return OS_ERR_RESOURCE;
    }

    if (p_mutex->Owner == NULL)
    {
        p_mutex->Owner = CurrentTCB;
        CurrentTCB->HeldMutex = p_mutex;
        p_mutex->NestCount = 1;
        OS_ExitCritical();
        return OS_OK;
    }
    else if (p_mutex->Owner == CurrentTCB)
    {
        /* 首次持有也占一层；在加一前拒绝 255，不能让 uint8_t 回绕。 */
        if (p_mutex->NestCount == UINT8_MAX)
        {
            OS_ExitCritical();
            return OS_ERR_NESTING;
        }
        p_mutex->NestCount++;
        OS_ExitCritical();
        return OS_OK;
    }
    else
    {
        if (CurrentTCB->Priority < p_mutex->Owner->Priority)
        {
            if (p_mutex->Owner->State == TASK_READY)
            {
                OS_ReadyListRemove(p_mutex->Owner);
                p_mutex->Owner->Priority = CurrentTCB->Priority;
                OS_ReadyListAdd(p_mutex->Owner);
            }
            else
            {
                p_mutex->Owner->Priority = CurrentTCB->Priority;
            }
        }
        CurrentTCB->State = TASK_BLOCKED;
        OS_ReadyListRemove(CurrentTCB);
        if (p_mutex->WaitList.Head == NULL)
        {
            List_InsertTail(&p_mutex->WaitList, CurrentTCB);
        }
        else
        {
            if (p_mutex->WaitList.Head->Priority > CurrentTCB->Priority)
            {
                CurrentTCB->Next = p_mutex->WaitList.Head;
                p_mutex->WaitList.Head->Prev = CurrentTCB;
                CurrentTCB->Prev = NULL;
                p_mutex->WaitList.Head = CurrentTCB;
            }
            else
            {
                OS_TCB *iter = p_mutex->WaitList.Head;
                while (iter->Next != NULL && iter->Next->Priority <= CurrentTCB->Priority)
                {
                    iter = iter->Next;
                }
                CurrentTCB->Next = iter->Next;
                CurrentTCB->Prev = iter;
                if (iter->Next != NULL) // 如果 iter 不是最后一个节点
                {
                    iter->Next->Prev = CurrentTCB; // 让后面的人指向我
                }
                else
                {
                    p_mutex->WaitList.Tail = CurrentTCB;
                }
                iter->Next = CurrentTCB;
            }
        }
        NextTCB = FindNextTask();
        OS_Schedule();
        OS_ExitCritical();
        return OS_OK;
    }
}

/**
 * @brief 释放一层同锁递归，最后一层直接交接所有权并恢复旧持有者优先级。
 * @param p_mutex 已初始化的锁；调用者必须为持有者。
 * @return OS_OK；空指针 OS_ERR_PARAM；非持有者 OS_ERR_NOT_OWNER。
 * @note 深度未归零时保留所有权与继承；最终交接先更新持锁记录和 READY 状态，
 *       再加入就绪队列并选择下一任务，不能让运行任务仍带 BLOCKED 状态。
 */
OS_Status OS_MutexPost(OS_Mutex *p_mutex)
{
    if (p_mutex == NULL)
        return OS_ERR_PARAM;

    OS_EnterCritical();

    if (p_mutex->Owner != CurrentTCB)
    {
        OS_ExitCritical();
        return OS_ERR_NOT_OWNER;
    }

    p_mutex->NestCount--;

    if (p_mutex->NestCount > 0)
    {
        OS_ExitCritical();
        return OS_OK;
    }

    /* 最后一层释放：先撤销本任务持锁记录，交接时再赋给新持有者。 */
    CurrentTCB->HeldMutex = NULL;

    if (CurrentTCB->Priority != CurrentTCB->OriginalPrio)
    {
        OS_ReadyListRemove(CurrentTCB);
        CurrentTCB->Priority = CurrentTCB->OriginalPrio;
        OS_ReadyListAdd(CurrentTCB);
    }
    if (p_mutex->WaitList.Head == NULL)
    {
        p_mutex->Owner = NULL;
        OS_ExitCritical();
        return OS_OK;
    }
    OS_TCB *TaskToWake = List_PopHead(&p_mutex->WaitList);
    p_mutex->Owner = TaskToWake;
    TaskToWake->HeldMutex = p_mutex;
    p_mutex->NestCount = 1;
    /* 等待者取得锁后将继续运行；继承路径依赖 State 判断就绪节点归属。 */
    TaskToWake->State = TASK_READY;
    OS_ReadyListAdd(TaskToWake);
    NextTCB = FindNextTask();

    OS_Schedule();
    OS_ExitCritical();
    return OS_OK;
}

void OS_QueueInit(OS_Queue *p_queue, void *buffer, uint16_t msg_size, uint16_t queue_size)
{
    if ((p_queue == NULL) || (buffer == NULL) || (msg_size == 0) || (queue_size == 0))
        return;

    p_queue->Buffer = buffer;
    p_queue->MsgSize = msg_size;
    p_queue->QSize = queue_size;
    p_queue->MsgCount = 0;
    p_queue->Head = 0;
    p_queue->Tail = 0;
    List_Init(&p_queue->WaitReadList);
}

OS_Status OS_QueueSend(OS_Queue *p_queue, void *p_msg)
{
    if (p_queue == NULL || p_msg == NULL)
        return OS_ERR_PARAM;

    OS_EnterCritical();

    if (p_queue->MsgCount >= p_queue->QSize) // 队列满
    {
        OS_ExitCritical();
        return OS_ERR_Q_FULL;
    }
    /* 处理写入地址 */
    uint8_t *WriteAddr = (uint8_t *)p_queue->Buffer + ((p_queue->Head) * (p_queue->MsgSize));
    /* 拷贝 */
    memcpy(WriteAddr, p_msg, p_queue->MsgSize);
    /* 处理写指针 Head = (Head + 1) % QSize 防止回绕 */
    p_queue->Head = (p_queue->Head + 1) % p_queue->QSize;
    /* 消息数 + 1 */
    p_queue->MsgCount++;

    if (p_queue->WaitReadList.Head != NULL)
        OS_TaskResumeAndSchedule(&p_queue->WaitReadList);

    OS_ExitCritical();

    return OS_OK;
}

/**
 * @brief ISR 非阻塞入队；复制、计数和唤醒位于同一独立保护区。
 * @param p_queue 已初始化且有效的队列。
 * @param p_msg 至少 MsgSize 字节的有效源，不能与队列内部状态或标记重叠。
 * @param p_HigherPrioTaskWoken 可选累计标记，只可能置 TRUE。
 * @return OS_OK；空参数 OS_ERR_PARAM；满队列 OS_ERR_Q_FULL，失败无副作用。
 * @note 不直接调度；原 PRIMASK 在每条返回路径恢复。
 */
OS_Status OS_QueueSendFromISR(OS_Queue *p_queue, void *p_msg, uint8_t *p_HigherPrioTaskWoken)
{
    if (p_queue == NULL || p_msg == NULL)
        return OS_ERR_PARAM;
    uint32_t saved_mask = OS_SaveIRQ();
    if (p_queue->MsgCount >= p_queue->QSize)
    {
        OS_RestoreIRQ(saved_mask);
        return OS_ERR_Q_FULL;
    }
    uint8_t *write_addr = (uint8_t *)p_queue->Buffer + p_queue->Head * p_queue->MsgSize;
    memcpy(write_addr, p_msg, p_queue->MsgSize);
    p_queue->Head = (p_queue->Head + 1) % p_queue->QSize;
    p_queue->MsgCount++;
    if (p_queue->WaitReadList.Head != NULL)
        OS_RecordISRWakeup(OS_TaskResume(&p_queue->WaitReadList), p_HigherPrioTaskWoken);
    OS_RestoreIRQ(saved_mask);
    return OS_OK;
}

/**
 * @brief 在线程中接收一条消息；空队列时 FIFO 无限等待。
 * @param p_queue 已初始化且仍有效的队列。
 * @param p_msg_buffer 可写空间至少为队列 MsgSize 字节的目标缓冲区。
 * @return OS_OK；空指针 OS_ERR_PARAM；Handler 中 OS_ERR_ISR；外层临界区或屏蔽调度时 OS_ERR_CRITICAL。
 * @note 拒绝路径不写目标缓冲区；醒后重新检查资源时不重复检查 API 自身的临界区。
 */
OS_Status OS_QueueReceive(OS_Queue *p_queue, void *p_msg_buffer)
{
    if (p_queue == NULL || p_msg_buffer == NULL)
        return OS_ERR_PARAM;

    OS_Status context = OS_CheckBlockingContext();
    if (context != OS_OK)
        return context;

    OS_EnterCritical();

    while (p_queue->MsgCount == 0) // 队列里无数据
    {
        /* 当前任务进入阻塞态，等待下一次切回来 */
        OS_TaskSuspend(&p_queue->WaitReadList);
        OS_ExitCritical();

        /* 回来了，此时重新查看队列里是否有数据 */
        OS_EnterCritical();
    }

    uint8_t *ReadAddr = (uint8_t *)p_queue->Buffer + ((p_queue->Tail) * (p_queue->MsgSize));
    memcpy(p_msg_buffer, ReadAddr, p_queue->MsgSize);
    p_queue->Tail = (p_queue->Tail + 1) % p_queue->QSize;
    p_queue->MsgCount--;

    OS_ExitCritical();
    return OS_OK;
}

/**
 * @brief ISR 非阻塞出队；空检查与数据复制在独立保护内完成。
 * @param p_queue 已初始化且有效的队列。
 * @param p_msg_buffer 至少 MsgSize 字节的目标，不能与队列状态或标记重叠。
 * @param p_HigherPrioTaskWoken 可选累计标记，M1 没有阻塞发送者，始终保持原值。
 * @return OS_OK；空参数 OS_ERR_PARAM；空队列 OS_ERR_RESOURCE，失败不写目标。
 * @note 不清除先前唤醒结果；不引入阶段 2B 的阻塞发送。
 */
OS_Status OS_QueueReceiveFromISR(OS_Queue *p_queue, void *p_msg_buffer, uint8_t *p_HigherPrioTaskWoken)
{
    if (p_queue == NULL || p_msg_buffer == NULL)
        return OS_ERR_PARAM;
    (void)p_HigherPrioTaskWoken;
    uint32_t saved_mask = OS_SaveIRQ();
    if (p_queue->MsgCount == 0)
    {
        OS_RestoreIRQ(saved_mask);
        return OS_ERR_RESOURCE;
    }
    uint8_t *read_addr = (uint8_t *)p_queue->Buffer + p_queue->Tail * p_queue->MsgSize;
    memcpy(p_msg_buffer, read_addr, p_queue->MsgSize);
    p_queue->Tail = (p_queue->Tail + 1) % p_queue->QSize;
    p_queue->MsgCount--;
    OS_RestoreIRQ(saved_mask);
    return OS_OK;
}

OS_Status OS_MemInit(OS_Mem *p_mem, void *start_addr, uint32_t block_num, uint32_t block_size)
{
    if (p_mem == NULL || start_addr == NULL || block_num == 0 || (block_size < OS_ALIGN_SIZE) || ((block_size & 0x03) != 0))
        return OS_ERR_PARAM;

    if (((uintptr_t)start_addr % OS_ALIGN_SIZE) != 0)
        return OS_ERR_NOT_ALIGN;

    p_mem->Addr = start_addr;
    p_mem->FreeList = start_addr;
    p_mem->BlockSize = block_size;
    p_mem->TotalBlocks = block_num;
    p_mem->FreeBlocks = block_num;
    List_Init(&p_mem->WaitList);

    void **pp_link;
    uint8_t *p_block = (uint8_t *)start_addr;
    for (uint32_t i = 0; i < (block_num - 1); ++i)
    {
        /* 取出当前块的起始地址 */
        pp_link = (void **)p_block;

        /* 计算下一个块的起始地址 */
        uint8_t *p_next = p_block + block_size;

        /* 在当前块的首地址写入下一块的首地址 */
        *pp_link = (void *)p_next;

        /* 移动到下一个块 */
        p_block = p_next;
    }

    /* 最后一个块写 NULL */
    pp_link = (void **)p_block;
    *pp_link = NULL;

    return OS_OK;
}

/**
 * @brief 在线程中获取一块内存；耗尽时 FIFO 无限等待。
 * @param p_mem 已初始化且仍有效的定长内存池。
 * @return 成功时块地址；空指针、Handler、外层临界区或屏蔽调度时 NULL。
 * @note 保持现有指针签名，M1 不区分 NULL 的具体错误；拒绝发生在摘取空闲块之前。
 */
void *OS_MemGet(OS_Mem *p_mem)
{
    if (p_mem == NULL)
        return NULL;

    OS_Status context = OS_CheckBlockingContext();
    if (context != OS_OK)
        return NULL;

    OS_EnterCritical();

    while (p_mem->FreeBlocks == 0)
    {
        OS_TaskSuspend(&p_mem->WaitList);
        OS_ExitCritical();

        OS_EnterCritical();
    }

    void *ret = p_mem->FreeList;
    p_mem->FreeList = *(void **)ret;
    p_mem->FreeBlocks--;
    OS_ExitCritical();

    return ret;
}

OS_Status OS_MemPut(OS_Mem *p_mem, void *p_block)
{
    if (p_mem == NULL || p_block == NULL)
        return OS_ERR_PARAM;

    OS_EnterCritical();

    /* 安全检查 */
    uint8_t *start_addr = (uint8_t *)p_mem->Addr;
    uint8_t *block_addr = (uint8_t *)p_block;
    uint32_t total_size = p_mem->TotalBlocks * p_mem->BlockSize;

    if (block_addr < start_addr || block_addr >= (start_addr + total_size))
    {
        OS_ExitCritical();
        return OS_ERR_INVALID_ADDR;
    }

    if (((uint32_t)(block_addr - start_addr) % p_mem->BlockSize) != 0)
    {
        OS_ExitCritical();
        return OS_ERR_NOT_ALIGN;
    }

    // 将当前的 FreeList (旧链表头) 存入要释放的块中
    *(void **)p_block = p_mem->FreeList;
    // 更新 FreeList 指向当前块(新链表头)
    p_mem->FreeList = p_block;
    p_mem->FreeBlocks++;

    OS_TaskResumeAndSchedule(&p_mem->WaitList);

    OS_ExitCritical();
    return OS_OK;
}

void OS_AssertFailed(const char *file, int line)
{
    OS_Disable_IRQ();
    printf("OS ASSERT FAILED !!!\r\n");
    printf("File: %s\r\n", file);
    printf("Line: %d\r\n", line);
    while (1)
    {
        // LED_Toggle();
        for (volatile int i = 0; i < 1000000; ++i)
            ;
    }
}

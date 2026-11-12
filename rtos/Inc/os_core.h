/**
 * @file    os_core.h
 * @author  SandOcean
 * @version V1.0
 * @date    2025-12-14
 * @brief   RTOS 核心内核头文件
 *
 * 本文件包含 RTOS 内核的核心定义与对外接口声明。
 */

/**
 * @mainpage SandOS 参考手册
 *
 * @section intro_sec 简介
 * 这是一个运行在 RISC-V (CH32V203) 上的轻量级实时操作系统。
 * 
 * @section features_sec 主要特性
 * - 抢占式优先级调度
 * - 任务管理 (创建, 延时)
 * - 信号量与互斥锁
 * - 消息队列
 * - 静态内存池管理
 * 
 * @section modules_sec 模块概览
 * - @ref Core       核心管理 (初始化, 临界区)
 * - @ref Task       任务管理
 * - @ref Semaphore  信号量
 * - @ref Mutex      互斥锁
 * - @ref Queue      消息队列
 * - @ref Memory     内存管理
 */

#ifndef __OS_CORE_H
#define __OS_CORE_H

#include "os_cpu.h"
#include <string.h> // For memcpy

/** @addtogroup Core 核心管理
 *  @{
 */

#define IDLE_STACK_SIZE 128     ///< 空闲任务栈大小
#define OS_MAX_PRIO 32          ///< 最大支持优先级数量 (0-31)
#define OS_STACK_MAGIC_VAL 0xDEADBEEF ///< 栈溢出检测魔法值
#define OS_ALIGN_SIZE   sizeof(void *) ///< 内存对齐字节数

/**
 * @brief  函数返回状态枚举
 */
typedef enum {
    OS_OK             = 0,  ///< 成功
    OS_ERR_PARAM      = 1,  ///< 参数错误（例如传入了 NULL 指针）
    OS_ERR_TIMEOUT    = 2,  ///< 等待超时（预留）
    OS_ERR_RESOURCE   = 3,  ///< 资源不可用（例如 TryLock 失败）
    OS_ERR_OVERFLOW   = 4,  ///< 计数达到容量上限，操作未改变资源计数
    
    // 互斥锁特有的错误
    OS_ERR_NOT_OWNER  = 10, ///< 错误：试图释放一个不属于自己的锁
    OS_ERR_NESTING    = 11, ///< 错误：递归嵌套层数超过限制（防止溢出）
    
    // 消息队列特有的错误
    OS_ERR_Q_FULL     = 15, ///< 错误：队列已满

    // 内存池特有的错误
    OS_ERR_INVALID_ADDR = 18, ///< 错误：传入的指针地址不落在原始内存
    OS_ERR_NOT_ALIGN = 19,    ///< 错误：地址未对齐

    // 系统级错误
    OS_ERR_ISR        = 20,   ///< 错误：在中断中调用了不能用的函数
    OS_ERR_CRITICAL   = 21,   ///< 阻塞接口在用户临界区或调度被屏蔽时拒绝，未改变状态
} OS_Status;

/** @} */ // end of group Core

/* 数据结构定义 -------------------------------------------------------- */

/** @addtogroup Task 任务管理
 *  @{
 */

/**
 * @brief  任务状态枚举
 */
typedef enum
{
    TASK_READY = 0, ///< 就绪：随时可以跑
    TASK_BLOCKED,   ///< 阻塞：在等时间，或者等信号量
    TASK_DELETED,   ///< 任务被删除
} OS_TaskState;

/**
 * @brief  任务控制块结构体定义
 */
typedef struct Task_Control_Block
{
    volatile uint32_t *stackPtr;     ///< 任务对应的栈指针
    volatile uint32_t *stackLimit;    ///< 栈底地址，用于栈溢出检测
    struct Task_Control_Block *Prev; ///< 指向上一个任务的指针
    struct Task_Control_Block *Next; ///< 指向下一个任务的指针
    OS_TaskState State;              ///< 任务状态
    volatile uint32_t DelayTicks;    ///< 延时的时间（单位ms）
    volatile uint8_t Priority;       ///< 任务优先级
    uint8_t OriginalPrio;            ///< 任务原始优先级
    struct Mutex *HeldMutex;        ///< M1 单锁所有权；NULL 表示未持锁，同锁递归不增加对象数量
} OS_TCB;


/**
 * @brief  任务链表结构体定义
 */
typedef struct List
{
    OS_TCB *Head;
    OS_TCB *Tail;
} OS_List;

/** @} */ // end of group Task

/** @addtogroup Semaphore 信号量
 *  @{
 */

/**
 * @brief  信号量结构体定义
 */
typedef struct Semaphore
{
    volatile uint16_t count;
    OS_List WaitList;
} OS_Sem;

/** @} */ // end of group Semaphore

/** @addtogroup Mutex 互斥锁
 *  @{
 */

/**
 * @brief  互斥锁结构体定义
 */
typedef struct Mutex
{
    OS_TCB* Owner;        ///< 互斥锁的主人
    OS_List WaitList;     ///< 正在等待此互斥锁的等待链表
    uint8_t NestCount;    ///< 包含首次持有的递归深度；1～255，0 表示未持有
    uint8_t OriginalPrio; ///< 原始优先级 
} OS_Mutex;

/** @} */ // end of group Mutex

/** @addtogroup Queue 消息队列
 *  @{
 */

/**
 * @brief  消息队列结构体定义
 */
typedef struct Queue
{
    void* Buffer;         ///< 指向存储数据的首地址
    uint16_t MsgSize;     ///< 每条消息的大小（单位字节）
    uint16_t QSize;       ///< 队列大小
    uint16_t MsgCount;    ///< 当前消息数（用于判断空/满）
    uint16_t Head;        ///< 写指针（实际上是下标）
    uint16_t Tail;        ///< 读指针（实际上是下标）
    /* 简化设计，当队列满时直接返回错误 */
    OS_List WaitReadList; ///< 读取等待链表
} OS_Queue;

/** @} */ // end of group Queue

/** @addtogroup Memory 内存管理
 *  @{
 */

/**
 * @brief 内存池控制块
 */
typedef struct MemBlock
{
    void   *Addr;           ///< 内存池的首地址
    void   *FreeList;       ///< 空闲块链表头指针
    uint32_t BlockSize;     ///< 每个块的大小
    uint32_t TotalBlocks;   ///< 总块数
    uint32_t FreeBlocks;    ///< 当前剩余空闲块数
    OS_List WaitList;       ///< 等待内存链表
} OS_Mem;

/** @} */ // end of group Memory


/* 全局变量声明 -------------------------------------------------------- */
extern volatile uint32_t g_SystemTickCount;
extern volatile uint32_t g_PrioMap;
extern volatile uint8_t g_OSRunning;
extern OS_List ReadyList[OS_MAX_PRIO];
extern OS_List DelayList;
extern OS_TCB *CurrentTCB;
extern OS_TCB *NextTCB;

/* 函数声明 ----------------------------------------------------------- */

/* Intrusive list helpers used by the kernel and isolated benchmark. */
void List_Init(OS_List *list);
void List_InsertTail(OS_List *list, OS_TCB *tcb);
void List_Remove(OS_List *list, OS_TCB *tcb);
OS_TCB *List_PopHead(OS_List *list);

/* DelayTicks is relative to the preceding node in a delay list. */
void OS_DelayListInsert(OS_List *list, OS_TCB *tcb, uint32_t ticks);
void OS_DelayListAdvance(OS_List *list);

/** @addtogroup Task
 *  @{
 */

/**
 * @brief  新建任务
 * 
 * @param  tcb                 任务对应的任务控制块指针，需用户分配内存
 * @param  task_function       任务入口函数地址，格式为 void func(void *param)
 * @param  task_param          传递给任务函数的参数指针
 * @param  stack_init_address  栈数组的起始地址（低地址），通常是数组名
 * @param  stack_depth         栈大小（单位：元素个数，即 uint32_t 的个数，而非字节数）
 * @param  priority            任务优先级 (0 ~ OS_MAX_PRIO-1)，数值越小优先级越高
 * 
 * @return OS_Status 
 * @retval OS_OK         创建成功
 * @retval OS_ERR_PARAM  参数无效（如指针为空或优先级越界）
 */
OS_Status OS_TaskCreate(OS_TCB *tcb,
                   OS_TaskFunc_t task_function,
                   void *task_param,
                   uint32_t *stack_init_address,
                   uint32_t stack_depth,
                   uint8_t priority);

/**
 * @brief  任务阻塞延时
 * @details ticks > 0 时进入阻塞状态。ticks == 0 时保持就绪，移到本优先级队尾；
 *          无同优先级就绪伙伴时继续运行，不进入延时队列。
 * @param  ticks 延时的时间长度（单位：SysTick 节拍数，通常为 ms）
 * @return 无；非法上下文先断言，不进入任何队列或请求调度。
 * @note Delay(0) 同样只能在线程且未屏蔽调度时调用；不改变 void 签名。
 */
void OS_Delay(uint32_t ticks);

/** @} */ // end of group Task


/** @addtogroup Core
 *  @{
 */

/**
 * @brief 初始化 OS 内核
 * @note  必须在 main 函数最开始调用，且在创建任何任务之前。
 *        它会初始化内部数据结构和空闲任务。
 */
void OS_Init(void);

/**
 * @brief  开启调度器
 * @note   此函数不会返回。它会启动 SysTick 并开始执行第一个任务。
 */
void OS_StartScheduler(void);

/**
 * @brief  处理 SysTick 中断的“回调函数”
 * @note   需要在 SysTick_Handler 中调用此函数以驱动系统心跳。
 */
void OS_Tick_Handler(void);

/**
 * @brief  进入临界区
 * @note   关闭全局中断并增加嵌套计数。
 */
void OS_EnterCritical(void);

/**
 * @brief  退出临界区
 * @note   减少嵌套计数，当计数为 0 时恢复全局中断。
 */
void OS_ExitCritical(void);

/** @} */ // end of group Core


/** @addtogroup Semaphore
 *  @{
 */

/**
 * @brief 初始化未发布且没有等待者的信号量，仅在线程/初始化上下文调用。
 * @param p_sem 指向静态分配且无并发访问的信号量对象。
 * @param init_count 初始许可次数，0～UINT16_MAX；不是时间参数。
 * @return 成功 OS_OK；p_sem 为 NULL 时 OS_ERR_PARAM。
 * @note 覆盖旧计数并清空等待表。不支持使用中的对象重新初始化。
 */
OS_Status OS_SemInit(OS_Sem *p_sem, uint16_t init_count);

/**
 * @brief 等待并消费一次许可，计数为 0 时无限阻塞。
 * @param p_sem 已初始化的信号量，在线程中调用。
 * @return 成功 OS_OK；空指针 OS_ERR_PARAM。
 * @note 无有限超时；ISR 返回 OS_ERR_ISR，外层临界区／屏蔽调度返回 OS_ERR_CRITICAL，拒绝无副作用。
 */
OS_Status OS_SemWait(OS_Sem *p_sem);

/**
 * @brief 在线程中释放一次许可；有等待者时按 FIFO 直接交接，计数不增加。
 * @param p_sem 已初始化的信号量。
 * @return OS_OK；空指针 OS_ERR_PARAM；无等待者且计数为 UINT16_MAX 时 OS_ERR_OVERFLOW。
 * @note 溢出失败保持计数与等待队列不变，计数不回绕。
 */
OS_Status OS_SemPost(OS_Sem *p_sem);

/**
 * @brief 在 ISR 中释放许可，不阻塞；计数上限与任务版相同。
 * @param p_sem 已初始化的信号量。
 * @param p_HigherPrioTaskWoken 可选输出；成功唤醒更高优先级任务时为 TRUE。
 * @return OS_OK、OS_ERR_PARAM 或 OS_ERR_OVERFLOW。
 * @note 失败不修改计数、队列或输出标记。成功标记累积、共享状态保护与
 *       ISR 尾部调度尚待专项修复，不能据计数测试宣称完整中断安全。
 */
OS_Status OS_SemPostFromISR(OS_Sem *p_sem, uint8_t *p_HigherPrioTaskWoken);

/** @} */ // end of group Semaphore


/** @addtogroup Mutex
 *  @{
 */

/**
 * @brief  初始化互斥锁
 * @param  p_mutex 指向互斥锁对象的指针
 * @return OS_Status
 */
OS_Status OS_MutexInit(OS_Mutex *p_mutex);

/**
 * @brief  申请互斥锁 (Lock)
 * @details 同锁递归与单锁直接优先级继承，不承诺多锁或传递继承。
 * @retval OS_ERR_NESTING 已持有同锁 255 层，状态不变。
 * @retval OS_ERR_RESOURCE 已持有另一把不同锁，立即拒绝且状态不变。
 * @note 线程上下文调用；ISR 返回 OS_ERR_ISR，用户临界区／屏蔽状态返回 OS_ERR_CRITICAL。
 * @param  p_mutex 指向互斥锁对象的指针
 * @return OS_Status
 */
OS_Status OS_MutexPend(OS_Mutex *p_mutex);

/**
 * @brief  释放互斥锁 (Unlock)
 * @details 只有锁的持有者才能释放锁。
 * @param  p_mutex 指向互斥锁对象的指针
 * @return OS_Status
 * @retval OS_OK 成功
 * @retval OS_ERR_NOT_OWNER 当前任务不是锁的持有者
 */
OS_Status OS_MutexPost(OS_Mutex *p_mutex);

/** @} */ // end of group Mutex


/** @addtogroup Queue
 *  @{
 */

/**
 * @brief  初始化队列
 * @param  p_queue    队列控制块指针
 * @param  buffer     实际存储区指针 (由用户分配的数组)
 * @param  msg_size   每个消息的大小 (字节)
 * @param  queue_size 队列深度 (最大能容纳的消息个数)
 */
void OS_QueueInit(OS_Queue *p_queue, void *buffer, uint16_t msg_size, uint16_t queue_size);

/**
 * @brief  发送消息（入队）
 * @details 将数据拷贝到队列缓冲区。如果队列满，则返回错误。
 * @param  p_queue 队列控制块指针
 * @param  p_msg   要发送的消息数据的指针
 * @return OS_Status
 * @retval OS_OK      发送成功
 * @retval OS_ERR_Q_FULL 队列已满
 */
OS_Status OS_QueueSend(OS_Queue *p_queue, void *p_msg);

/**
 * @brief  接收消息（出队）
 * @details 如果队列为空，任务将阻塞直到有消息到达。
 * @param  p_queue      队列控制块指针
 * @param  p_msg_buffer 用于接收消息的缓冲区指针
 * @return OS_OK；空参数 OS_ERR_PARAM；ISR 中 OS_ERR_ISR；外层临界区／屏蔽调度 OS_ERR_CRITICAL。
 * @note 非法上下文不消费消息、不写目标缓冲区。
 */
OS_Status OS_QueueReceive(OS_Queue *p_queue, void *p_msg_buffer);

/**
 * @brief  在中断中发送消息（入队）
 * @details 中断安全版本，不会阻塞。
 * @param  p_queue   队列控制块指针
 * @param  p_msg     要发送的消息数据的指针
 * @param  p_HigherPrioTaskWoken 输出参数，如果唤醒了更高优先级任务则置为 TRUE
 * @return OS_Status
 * @retval OS_OK         发送成功
 * @retval OS_ERR_Q_FULL 队列已满
 * @retval OS_ERR_PARAM  参数无效
 */
OS_Status OS_QueueSendFromISR(OS_Queue *p_queue, void *p_msg, uint8_t *p_HigherPrioTaskWoken);

/**
 * @brief  在中断中接收消息（出队）
 * @details 中断安全版本，不会阻塞。如果队列为空则立即返回错误。
 * @param  p_queue      队列控制块指针
 * @param  p_msg_buffer 用于接收消息的缓冲区指针
 * @param  p_HigherPrioTaskWoken 输出参数（预留，当前未使用）
 * @return OS_Status
 * @retval OS_OK          接收成功
 * @retval OS_ERR_RESOURCE 队列为空
 * @retval OS_ERR_PARAM   参数无效
 */
OS_Status OS_QueueReceiveFromISR(OS_Queue *p_queue, void *p_msg_buffer, uint8_t *p_HigherPrioTaskWoken);


/** @} */ // end of group Queue


/** @addtogroup Memory
 *  @{
 */

/**
 * @brief  初始化固定大小内存池
 * @details 将一块连续的内存区域划分为若干个固定大小的块，并使用链表管理。
 * @param  p_mem      内存池对象指针
 * @param  start_addr 内存池起始地址（需保证4字节对齐）
 * @param  block_num  内存块总数量
 * @param  block_size 单个内存块大小（字节，需保证4字节对齐且 >= 指针大小）
 */
OS_Status OS_MemInit(OS_Mem *p_mem, void *start_addr, uint32_t block_num, uint32_t block_size);

/**
 * @brief  申请内存块
 * @details 从内存池中获取一个空闲块。如果没有空闲块，任务将阻塞。
 * @param  p_mem 内存池对象指针
 * @return 指向申请到的内存块；空对象、ISR 或调用者屏蔽调度时返回 NULL。
 * @note 即使有空闲块也先检查上下文；非法上下文不消耗资源。
 */
void* OS_MemGet(OS_Mem *p_mem);

/**
 * @brief  释放内存块
 * @details 将内存块归还给内存池。
 * @param  p_mem   内存池对象指针
 * @param  p_block 待释放的内存块地址
 * @return OS_Status
 * @retval OS_OK 成功
 * @retval OS_ERR_INVALID_ADDR 地址不在该内存池范围内
 * @retval OS_ERR_NOT_ALIGN    地址未对齐
 */
OS_Status OS_MemPut(OS_Mem *p_mem, void *p_block);

/** @} */ // end of group Memory

#endif /* __OS_CORE_H */

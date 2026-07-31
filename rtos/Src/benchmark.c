#include "benchmark.h"

volatile uint8_t g_BenchmarkRuntimeActive = 0U;

#if OS_BENCHMARK_ENABLE

#include "os_core.h"

#include <stdio.h>

#define BENCH_MICRO_SAMPLES            128U
#define BENCH_WORKLOAD_SAMPLES          8U
#define BENCH_MAX_TASKS                 64U
#define BENCH_POOL_BLOCKS               64U
#define BENCH_POOL_BLOCK_SIZE           16U
#define BENCH_QUEUE_MAX_ITEMS           64U
#define BENCH_DELAY_INTERVAL            10U
#define BENCH_WORKLOAD_TICKS            1024U
#define BENCH_RUNTIME_WORKERS            8U
#define BENCH_RUNTIME_TICKS              256U
#define BENCH_RUNTIME_WORKER_STACK_WORDS 96U
#define BENCH_RUNTIME_CONTROL_STACK_WORDS 256U

#if defined(__CC_ARM)
#define BENCH_NOINLINE __attribute__((noinline))
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif

typedef void (*Benchmark_Operation)(void *context);
typedef void (*Benchmark_Prepare)(void *context);

typedef struct
{
    uint32_t min;
    uint32_t median;
    uint32_t p95;
    uint32_t max;
} Benchmark_Stats;

typedef struct
{
    uint8_t priority;
} Benchmark_SelectContext;

typedef struct
{
    uint32_t count;
    uint32_t target_index;
} Benchmark_ListContext;

typedef struct
{
    uint32_t count;
    uint32_t insert_ticks;
} Benchmark_DelayInsertContext;

typedef enum
{
    BENCH_TICK_NONE_DUE = 0,
    BENCH_TICK_ONE_DUE,
    BENCH_TICK_BURST_DUE
} Benchmark_TickScenario;

typedef struct
{
    uint32_t count;
    Benchmark_TickScenario scenario;
} Benchmark_TickContext;

typedef enum
{
    BENCH_PROFILE_SPARSE = 0,
    BENCH_PROFILE_DENSE,
    BENCH_PROFILE_BURST
} Benchmark_WorkloadProfile;

typedef struct
{
    uint32_t count;
    uint32_t ticks;
    Benchmark_WorkloadProfile profile;
} Benchmark_WorkloadContext;

typedef struct
{
    uint32_t count;
} Benchmark_PoolContext;

typedef struct
{
    uint32_t capacity;
} Benchmark_QueueContext;

typedef struct Benchmark_SingleNode
{
    struct Benchmark_SingleNode *next;
} Benchmark_SingleNode;

typedef struct
{
    OS_TCB tcb;
    uint32_t stack[BENCH_RUNTIME_WORKER_STACK_WORDS];
    uint32_t period;
    volatile uint32_t wake_count;
} Benchmark_RuntimeWorker;

static uint8_t s_dwt_available = FALSE;
static uint32_t s_probe_overhead = 0U;
static uint32_t s_samples[BENCH_MICRO_SAMPLES];
static volatile uintptr_t s_sink = 0U;
static OS_TCB *volatile s_last_tcb = NULL;
static volatile uint32_t s_priority_map = 0U;

/* Shared scratch storage keeps the suite below the STM32F103C8 RAM limit. */
static OS_TCB s_nodes_a[BENCH_MAX_TASKS];
static OS_TCB s_nodes_b[BENCH_MAX_TASKS];
static OS_List s_ready_a[OS_MAX_PRIO];
static OS_List s_ready_b[OS_MAX_PRIO];
static OS_List s_delay_list_a;
static Benchmark_SingleNode s_single_nodes[BENCH_MAX_TASKS];
static Benchmark_SingleNode *s_single_head = NULL;
static uint32_t s_periods[BENCH_MAX_TASKS];
static uint32_t s_wake_count_a[BENCH_MAX_TASKS];
static uint32_t s_wake_count_b[BENCH_MAX_TASKS];
static uint32_t s_expected_deadlines[BENCH_MAX_TASKS + 1U];
static uint32_t s_pool_storage[(BENCH_POOL_BLOCKS * BENCH_POOL_BLOCK_SIZE) / sizeof(uint32_t)];
static uint8_t s_pool_used[BENCH_POOL_BLOCKS];
static OS_Mem s_pool;
static uint32_t s_queue_buffer[BENCH_QUEUE_MAX_ITEMS];
static uint32_t s_queue_linear[BENCH_QUEUE_MAX_ITEMS];
static OS_Queue s_queue;
static uint32_t s_queue_linear_count = 0U;
static Benchmark_RuntimeWorker s_runtime_workers[BENCH_RUNTIME_WORKERS];
static OS_TCB s_runtime_control_tcb;
static uint32_t s_runtime_control_stack[BENCH_RUNTIME_CONTROL_STACK_WORDS];
static volatile uint32_t s_runtime_tick_probe = 0U;
static volatile uint32_t s_runtime_tick_count = 0U;
static volatile uint32_t s_runtime_tick_total = 0U;
static volatile uint32_t s_runtime_tick_min = 0U;
static volatile uint32_t s_runtime_tick_max = 0U;

static void Benchmark_ClearTCB(OS_TCB *tcb, uint8_t priority)
{
    tcb->stackPtr = NULL;
    tcb->stackLimit = NULL;
    tcb->Prev = NULL;
    tcb->Next = NULL;
    tcb->State = TASK_BLOCKED;
    tcb->DelayTicks = 0U;
    tcb->Priority = priority;
    tcb->OriginalPrio = priority;
}

static void Benchmark_InitReadyLists(OS_List *ready_lists)
{
    uint32_t i;

    for (i = 0U; i < OS_MAX_PRIO; ++i)
    {
        List_Init(&ready_lists[i]);
    }
}

static void Benchmark_ReadyAdd(OS_List *ready_lists, uint32_t *prio_map, OS_TCB *tcb)
{
    *prio_map |= (1UL << tcb->Priority);
    List_InsertTail(&ready_lists[tcb->Priority], tcb);
}

static uint32_t Benchmark_CountList(const OS_List *list)
{
    uint32_t count = 0U;
    const OS_TCB *node = list->Head;

    while (node != NULL)
    {
        ++count;
        node = node->Next;
    }

    return count;
}

static uint32_t Benchmark_CountReadyLists(const OS_List *ready_lists)
{
    uint32_t count = 0U;
    uint32_t priority;

    for (priority = 0U; priority < OS_MAX_PRIO; ++priority)
    {
        count += Benchmark_CountList(&ready_lists[priority]);
    }

    return count;
}

static uint8_t Benchmark_ValidateDelayList(const OS_List *list)
{
    const OS_TCB *previous = NULL;
    const OS_TCB *node = list->Head;

    while (node != NULL)
    {
        if (node->Prev != previous)
        {
            return FALSE;
        }
        previous = node;
        node = node->Next;
    }

    return (previous == list->Tail) ? TRUE : FALSE;
}

static uint8_t Benchmark_DWTInit(void)
{
    volatile uint32_t delay;
    uint32_t start;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();

    start = DWT->CYCCNT;
    for (delay = 0U; delay < 32U; ++delay)
    {
        __NOP();
    }

    return ((DWT->CYCCNT - start) != 0U) ? TRUE : FALSE;
}

uint8_t Benchmark_IsDWTAvailable(void)
{
    return s_dwt_available;
}

static BENCH_NOINLINE void Benchmark_EmptyOperation(void *context)
{
    (void)context;
}

static uint32_t Benchmark_Measure(Benchmark_Operation operation, void *context)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t start;
    uint32_t elapsed;

    /* Keep nested OS critical sections inside the timed region from re-enabling IRQs. */
    OS_EnterCritical();
    __DSB();
    __ISB();
    start = DWT->CYCCNT;

    operation(context);

    __DSB();
    __ISB();
    elapsed = DWT->CYCCNT - start;
    OS_ExitCritical();

    if (primask != 0U)
    {
        __disable_irq();
    }

    return elapsed;
}

static void Benchmark_Sort(uint32_t *values, uint32_t count)
{
    uint32_t i;

    for (i = 1U; i < count; ++i)
    {
        uint32_t value = values[i];
        uint32_t j = i;

        while ((j > 0U) && (values[j - 1U] > value))
        {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = value;
    }
}

static void Benchmark_BuildStats(uint32_t *samples, uint32_t count, uint32_t probe, Benchmark_Stats *stats)
{
    uint32_t i;
    uint32_t p95_index;

    for (i = 0U; i < count; ++i)
    {
        samples[i] = (samples[i] > probe) ? (samples[i] - probe) : 0U;
    }

    Benchmark_Sort(samples, count);
    p95_index = ((count * 95U) + 99U) / 100U;
    if (p95_index > 0U)
    {
        --p95_index;
    }

    stats->min = samples[0];
    stats->median = samples[count / 2U];
    stats->p95 = samples[p95_index];
    stats->max = samples[count - 1U];
}

static void Benchmark_Collect(Benchmark_Operation operation,
                              Benchmark_Prepare prepare,
                              void *context,
                              uint32_t sample_count,
                              uint32_t probe,
                              Benchmark_Stats *stats)
{
    uint32_t i;

    for (i = 0U; i < sample_count; ++i)
    {
        prepare(context);
        s_samples[i] = Benchmark_Measure(operation, context);
    }

    Benchmark_BuildStats(s_samples, sample_count, probe, stats);
}

static uint32_t Benchmark_CalibrateProbe(void)
{
    Benchmark_Stats stats;

    Benchmark_Collect(Benchmark_EmptyOperation,
                      Benchmark_EmptyOperation,
                      NULL,
                      BENCH_MICRO_SAMPLES,
                      0U,
                      &stats);
    return stats.min;
}

static uint32_t Benchmark_CalibrateRuntimeTickProbe(void)
{
    uint32_t minimum = 0xFFFFFFFFUL;
    uint32_t i;

    for (i = 0U; i < 32U; ++i)
    {
        uint32_t start = DWT->CYCCNT;
        uint32_t elapsed = DWT->CYCCNT - start;

        if (elapsed < minimum)
        {
            minimum = elapsed;
        }
    }

    return minimum;
}

static void Benchmark_Report(const char *name,
                             const char *scenario,
                             uint32_t count,
                             const Benchmark_Stats *reference,
                             const Benchmark_Stats *optimized)
{
    int32_t saved_x100 = 0;
    uint32_t speedup_x100 = 0U;
    int32_t absolute_saved;

    if (reference->median != 0U)
    {
        saved_x100 = (int32_t)(((int64_t)((int32_t)reference->median - (int32_t)optimized->median) * 10000) /
                                (int64_t)reference->median);
    }
    if (optimized->median != 0U)
    {
        speedup_x100 = (uint32_t)(((uint64_t)reference->median * 100U) / optimized->median);
    }

    absolute_saved = (saved_x100 < 0) ? -saved_x100 : saved_x100;
    printf("BENCH,%s,%s,N=%lu,ref_min=%lu,ref_p50=%lu,ref_p95=%lu,opt_min=%lu,opt_p50=%lu,opt_p95=%lu,saved_cycles=%ld,saved_p50=%s%ld.%02ld%%,speedup=%lu.%02lux\r\n",
           name,
           scenario,
           (unsigned long)count,
           (unsigned long)reference->min,
           (unsigned long)reference->median,
           (unsigned long)reference->p95,
           (unsigned long)optimized->min,
           (unsigned long)optimized->median,
           (unsigned long)optimized->p95,
           (long)((int32_t)reference->median - (int32_t)optimized->median),
           (saved_x100 < 0) ? "-" : "",
           (long)(absolute_saved / 100),
           (long)(absolute_saved % 100),
           (unsigned long)(speedup_x100 / 100U),
           (unsigned long)(speedup_x100 % 100U));
}

static void Benchmark_SelectPrepare(void *context)
{
    Benchmark_SelectContext *select = (Benchmark_SelectContext *)context;
    uint32_t priority;

    Benchmark_InitReadyLists(s_ready_a);
    for (priority = 0U; priority < OS_MAX_PRIO; ++priority)
    {
        Benchmark_ClearTCB(&s_nodes_a[priority], (uint8_t)priority);
    }

    s_nodes_a[select->priority].State = TASK_READY;
    List_InsertTail(&s_ready_a[select->priority], &s_nodes_a[select->priority]);
    s_priority_map = (1UL << select->priority);
}

static BENCH_NOINLINE void Benchmark_SelectLinear(void *context)
{
    Benchmark_SelectContext *select = (Benchmark_SelectContext *)context;
    uint32_t priority;

    (void)select;
    s_last_tcb = NULL;
    for (priority = 0U; priority < OS_MAX_PRIO; ++priority)
    {
        if (s_ready_a[priority].Head != NULL)
        {
            s_last_tcb = s_ready_a[priority].Head;
            break;
        }
    }
    s_sink ^= (uintptr_t)s_last_tcb;
}

static BENCH_NOINLINE void Benchmark_SelectBitmap(void *context)
{
    uint8_t priority = OS_GetTopPrio(s_priority_map);

    (void)context;
    s_last_tcb = s_ready_a[priority].Head;
    s_sink ^= (uintptr_t)s_last_tcb;
}

static void Benchmark_ListPrepare(void *context)
{
    Benchmark_ListContext *list_context = (Benchmark_ListContext *)context;
    uint32_t i;

    List_Init(&s_delay_list_a);
    for (i = 0U; i < list_context->count; ++i)
    {
        Benchmark_ClearTCB(&s_nodes_a[i], 0U);
        List_InsertTail(&s_delay_list_a, &s_nodes_a[i]);
        s_single_nodes[i].next = (i + 1U < list_context->count) ? &s_single_nodes[i + 1U] : NULL;
    }

    s_single_head = &s_single_nodes[0];
}

static BENCH_NOINLINE void Benchmark_ListRemoveDoubly(void *context)
{
    Benchmark_ListContext *list_context = (Benchmark_ListContext *)context;
    OS_TCB *target = &s_nodes_a[list_context->target_index];

    List_Remove(&s_delay_list_a, target);
    s_sink ^= (uintptr_t)s_delay_list_a.Head;
}

static BENCH_NOINLINE void Benchmark_ListRemoveLinear(void *context)
{
    Benchmark_ListContext *list_context = (Benchmark_ListContext *)context;
    Benchmark_SingleNode *target = &s_single_nodes[list_context->target_index];
    Benchmark_SingleNode *previous = NULL;
    Benchmark_SingleNode *node = s_single_head;

    while ((node != NULL) && (node != target))
    {
        previous = node;
        node = node->next;
    }

    if (node != NULL)
    {
        if (previous == NULL)
        {
            s_single_head = node->next;
        }
        else
        {
            previous->next = node->next;
        }
    }
    s_sink ^= (uintptr_t)s_single_head;
}

static void Benchmark_BuildDelayList(OS_List *list, OS_TCB *nodes, uint32_t count, uint32_t first_ticks)
{
    uint32_t i;

    List_Init(list);
    for (i = 0U; i < count; ++i)
    {
        Benchmark_ClearTCB(&nodes[i], (uint8_t)(i % OS_MAX_PRIO));
        OS_DelayListInsert(list, &nodes[i], first_ticks + (i * BENCH_DELAY_INTERVAL));
    }
}

static void Benchmark_DelayInsertPrepare(void *context)
{
    Benchmark_DelayInsertContext *insert = (Benchmark_DelayInsertContext *)context;

    Benchmark_BuildDelayList(&s_delay_list_a, s_nodes_a, insert->count, BENCH_DELAY_INTERVAL);
    Benchmark_ClearTCB(&s_nodes_b[0], 0U);
}

static BENCH_NOINLINE void Benchmark_DelayInsertReference(void *context)
{
    Benchmark_DelayInsertContext *insert = (Benchmark_DelayInsertContext *)context;

    s_nodes_b[0].DelayTicks = insert->insert_ticks;
    s_nodes_b[0].State = TASK_BLOCKED;
    s_sink ^= s_nodes_b[0].DelayTicks;
}

static BENCH_NOINLINE void Benchmark_DelayInsertDelta(void *context)
{
    Benchmark_DelayInsertContext *insert = (Benchmark_DelayInsertContext *)context;

    OS_DelayListInsert(&s_delay_list_a, &s_nodes_b[0], insert->insert_ticks);
    s_sink ^= s_nodes_b[0].DelayTicks;
}

static uint8_t Benchmark_ValidateDelayInsertion(const Benchmark_DelayInsertContext *insert)
{
    uint32_t i;
    uint32_t j;
    uint32_t elapsed = 0U;
    OS_TCB *node;

    if (Benchmark_ValidateDelayList(&s_delay_list_a) == FALSE)
    {
        return FALSE;
    }

    for (i = 0U; i < insert->count; ++i)
    {
        s_expected_deadlines[i] = BENCH_DELAY_INTERVAL + (i * BENCH_DELAY_INTERVAL);
    }
    s_expected_deadlines[insert->count] = insert->insert_ticks;

    for (i = 1U; i <= insert->count; ++i)
    {
        uint32_t value = s_expected_deadlines[i];
        j = i;
        while ((j > 0U) && (s_expected_deadlines[j - 1U] > value))
        {
            s_expected_deadlines[j] = s_expected_deadlines[j - 1U];
            --j;
        }
        s_expected_deadlines[j] = value;
    }

    node = s_delay_list_a.Head;
    for (i = 0U; i <= insert->count; ++i)
    {
        if (node == NULL)
        {
            return FALSE;
        }
        elapsed += node->DelayTicks;
        if (elapsed != s_expected_deadlines[i])
        {
            return FALSE;
        }
        node = node->Next;
    }

    return (node == NULL) ? TRUE : FALSE;
}

static uint32_t Benchmark_TickDelayFor(uint32_t index, Benchmark_TickScenario scenario)
{
    if (scenario == BENCH_TICK_BURST_DUE)
    {
        return 1U;
    }
    if ((scenario == BENCH_TICK_ONE_DUE) && (index == 0U))
    {
        return 1U;
    }
    return 10U + (index * BENCH_DELAY_INTERVAL);
}

static void Benchmark_TickPrepare(void *context)
{
    Benchmark_TickContext *tick = (Benchmark_TickContext *)context;
    uint32_t i;

    Benchmark_InitReadyLists(s_ready_a);
    Benchmark_InitReadyLists(s_ready_b);
    List_Init(&s_delay_list_a);
    for (i = 0U; i < tick->count; ++i)
    {
        uint32_t delay = Benchmark_TickDelayFor(i, tick->scenario);

        Benchmark_ClearTCB(&s_nodes_a[i], (uint8_t)(i % OS_MAX_PRIO));
        Benchmark_ClearTCB(&s_nodes_b[i], (uint8_t)(i % OS_MAX_PRIO));
        OS_DelayListInsert(&s_delay_list_a, &s_nodes_a[i], delay);
        s_nodes_b[i].DelayTicks = delay;
    }
}

static OS_TCB *Benchmark_PopExpired(OS_List *list)
{
    if (list->Head == NULL || list->Head->DelayTicks != 0U)
    {
        return NULL;
    }

    return List_PopHead(list);
}

static BENCH_NOINLINE void Benchmark_DelayTickReference(void *context)
{
    Benchmark_TickContext *tick = (Benchmark_TickContext *)context;
    uint32_t priority_map = 0U;
    uint32_t i;

    for (i = 0U; i < tick->count; ++i)
    {
        OS_TCB *tcb = &s_nodes_b[i];

        if ((tcb->State == TASK_BLOCKED) && (tcb->DelayTicks > 0U))
        {
            --tcb->DelayTicks;
            if (tcb->DelayTicks == 0U)
            {
                tcb->State = TASK_READY;
                Benchmark_ReadyAdd(s_ready_b, &priority_map, tcb);
            }
        }
    }
    s_sink ^= priority_map;
}

static BENCH_NOINLINE void Benchmark_DelayTickDelta(void *context)
{
    OS_TCB *woken;
    uint32_t priority_map = 0U;

    (void)context;
    OS_DelayListAdvance(&s_delay_list_a);
    woken = Benchmark_PopExpired(&s_delay_list_a);
    while (woken != NULL)
    {
        woken->State = TASK_READY;
        Benchmark_ReadyAdd(s_ready_a, &priority_map, woken);
        woken = Benchmark_PopExpired(&s_delay_list_a);
    }
    s_sink ^= priority_map;
}

static uint8_t Benchmark_ValidateTick(const Benchmark_TickContext *tick)
{
    uint32_t expected = 0U;
    uint32_t i;

    if (tick->scenario == BENCH_TICK_ONE_DUE)
    {
        expected = 1U;
    }
    else if (tick->scenario == BENCH_TICK_BURST_DUE)
    {
        expected = tick->count;
    }

    if ((Benchmark_CountReadyLists(s_ready_a) != expected) ||
        (Benchmark_CountReadyLists(s_ready_b) != expected) ||
        (Benchmark_ValidateDelayList(&s_delay_list_a) == FALSE))
    {
        return FALSE;
    }

    for (i = 0U; i < tick->count; ++i)
    {
        if (s_nodes_a[i].State != s_nodes_b[i].State)
        {
            return FALSE;
        }
    }
    return TRUE;
}

static uint32_t Benchmark_PeriodFor(uint32_t index, Benchmark_WorkloadProfile profile)
{
    if (profile == BENCH_PROFILE_DENSE)
    {
        return 1U + (index % 4U);
    }
    if (profile == BENCH_PROFILE_BURST)
    {
        return 8U;
    }
    return 17U + ((index * 37U) % 997U);
}

static void Benchmark_WorkloadPrepare(void *context)
{
    Benchmark_WorkloadContext *workload = (Benchmark_WorkloadContext *)context;
    uint32_t i;

    List_Init(&s_delay_list_a);
    for (i = 0U; i < workload->count; ++i)
    {
        s_periods[i] = Benchmark_PeriodFor(i, workload->profile);
        s_wake_count_a[i] = 0U;
        s_wake_count_b[i] = 0U;
        Benchmark_ClearTCB(&s_nodes_a[i], (uint8_t)(i % OS_MAX_PRIO));
        Benchmark_ClearTCB(&s_nodes_b[i], (uint8_t)(i % OS_MAX_PRIO));
        OS_DelayListInsert(&s_delay_list_a, &s_nodes_a[i], s_periods[i]);
        s_nodes_b[i].DelayTicks = s_periods[i];
    }
}

static BENCH_NOINLINE void Benchmark_WorkloadLinear(void *context)
{
    Benchmark_WorkloadContext *workload = (Benchmark_WorkloadContext *)context;
    uint32_t tick;
    uint32_t i;

    for (tick = 0U; tick < workload->ticks; ++tick)
    {
        for (i = 0U; i < workload->count; ++i)
        {
            OS_TCB *tcb = &s_nodes_b[i];

            if (tcb->DelayTicks > 0U)
            {
                --tcb->DelayTicks;
                if (tcb->DelayTicks == 0U)
                {
                    ++s_wake_count_b[i];
                    tcb->DelayTicks = s_periods[i];
                }
            }
        }
    }
    s_sink ^= s_wake_count_b[0];
}

static BENCH_NOINLINE void Benchmark_WorkloadDelta(void *context)
{
    Benchmark_WorkloadContext *workload = (Benchmark_WorkloadContext *)context;
    uint32_t tick;

    for (tick = 0U; tick < workload->ticks; ++tick)
    {
        OS_TCB *woken;

        OS_DelayListAdvance(&s_delay_list_a);
        woken = Benchmark_PopExpired(&s_delay_list_a);
        while (woken != NULL)
        {
            uint32_t index = (uint32_t)(woken - s_nodes_a);

            ++s_wake_count_a[index];
            OS_DelayListInsert(&s_delay_list_a, woken, s_periods[index]);
            woken = Benchmark_PopExpired(&s_delay_list_a);
        }
    }
    s_sink ^= s_wake_count_a[0];
}

static uint8_t Benchmark_ValidateWorkload(const Benchmark_WorkloadContext *workload)
{
    uint32_t i;

    if (Benchmark_ValidateDelayList(&s_delay_list_a) == FALSE)
    {
        return FALSE;
    }

    for (i = 0U; i < workload->count; ++i)
    {
        if (s_wake_count_a[i] != s_wake_count_b[i])
        {
            return FALSE;
        }
    }
    return TRUE;
}

static void Benchmark_PoolPrepare(void *context)
{
    Benchmark_PoolContext *pool_context = (Benchmark_PoolContext *)context;
    uint32_t i;

    (void)OS_MemInit(&s_pool,
                      s_pool_storage,
                      pool_context->count,
                      BENCH_POOL_BLOCK_SIZE);
    for (i = 0U; i < pool_context->count; ++i)
    {
        s_pool_used[i] = (i + 1U < pool_context->count) ? TRUE : FALSE;
    }

    /* Leave only the final block free so the scan reference exercises O(N). */
    for (i = 0U; i + 1U < pool_context->count; ++i)
    {
        (void)OS_MemGet(&s_pool);
    }
}

static BENCH_NOINLINE void Benchmark_PoolLinear(void *context)
{
    Benchmark_PoolContext *pool_context = (Benchmark_PoolContext *)context;
    uint32_t i;

    for (i = 0U; i < pool_context->count; ++i)
    {
        if (s_pool_used[i] == FALSE)
        {
            s_pool_used[i] = TRUE;
            break;
        }
    }
    if (i < pool_context->count)
    {
        s_pool_used[i] = FALSE;
    }
    s_sink ^= i;
}

static BENCH_NOINLINE void Benchmark_PoolFreeList(void *context)
{
    void *block;

    (void)context;
    block = OS_MemGet(&s_pool);
    if (block != NULL)
    {
        (void)OS_MemPut(&s_pool, block);
    }
    s_sink ^= (uintptr_t)block;
}

static void Benchmark_QueuePrepare(void *context)
{
    Benchmark_QueueContext *queue_context = (Benchmark_QueueContext *)context;
    uint32_t i;

    OS_QueueInit(&s_queue,
                 s_queue_buffer,
                 (uint16_t)sizeof(uint32_t),
                 (uint16_t)queue_context->capacity);
    s_queue_linear_count = queue_context->capacity - 1U;

    for (i = 0U; i < s_queue_linear_count; ++i)
    {
        uint32_t value = i + 1U;

        (void)OS_QueueSend(&s_queue, &value);
        s_queue_linear[i] = value;
    }
}

static BENCH_NOINLINE void Benchmark_QueueLinear(void *context)
{
    Benchmark_QueueContext *queue_context = (Benchmark_QueueContext *)context;
    uint32_t value = 0xA55A0000UL | queue_context->capacity;
    uint32_t output;
    uint32_t i;

    s_queue_linear[s_queue_linear_count++] = value;
    output = s_queue_linear[0];
    for (i = 1U; i < s_queue_linear_count; ++i)
    {
        s_queue_linear[i - 1U] = s_queue_linear[i];
    }
    --s_queue_linear_count;
    s_sink ^= output;
}

static BENCH_NOINLINE void Benchmark_QueueRing(void *context)
{
    Benchmark_QueueContext *queue_context = (Benchmark_QueueContext *)context;
    uint32_t value = 0xA55A0000UL | queue_context->capacity;
    uint32_t output = 0U;

    (void)OS_QueueSend(&s_queue, &value);
    (void)OS_QueueReceive(&s_queue, &output);
    s_sink ^= output;
}

static uint8_t Benchmark_RunPriorityTests(void)
{
    static const uint8_t priorities[] = {0U, 15U, 31U};
    static const char *const names[] = {"first", "middle", "last"};
    Benchmark_SelectContext context;
    Benchmark_Stats reference;
    Benchmark_Stats optimized;
    uint32_t i;

    for (i = 0U; i < (sizeof(priorities) / sizeof(priorities[0])); ++i)
    {
        context.priority = priorities[i];
        Benchmark_Collect(Benchmark_SelectLinear, Benchmark_SelectPrepare, &context,
                          BENCH_MICRO_SAMPLES, s_probe_overhead, &reference);
        Benchmark_Collect(Benchmark_SelectBitmap, Benchmark_SelectPrepare, &context,
                          BENCH_MICRO_SAMPLES, s_probe_overhead, &optimized);
        Benchmark_Report("prio_select", names[i], OS_MAX_PRIO, &reference, &optimized);

        Benchmark_SelectPrepare(&context);
        Benchmark_SelectLinear(&context);
        if (s_last_tcb != &s_nodes_a[context.priority])
        {
            return FALSE;
        }
        Benchmark_SelectPrepare(&context);
        Benchmark_SelectBitmap(&context);
        if (s_last_tcb != &s_nodes_a[context.priority])
        {
            return FALSE;
        }
    }

    return TRUE;
}

static uint8_t Benchmark_RunListTests(void)
{
    static const uint32_t counts[] = {1U, 4U, 16U, 64U};
    Benchmark_ListContext context;
    Benchmark_Stats reference;
    Benchmark_Stats optimized;
    uint32_t i;

    for (i = 0U; i < (sizeof(counts) / sizeof(counts[0])); ++i)
    {
        context.count = counts[i];
        context.target_index = counts[i] / 2U;
        Benchmark_Collect(Benchmark_ListRemoveLinear, Benchmark_ListPrepare, &context,
                          BENCH_MICRO_SAMPLES, s_probe_overhead, &reference);
        Benchmark_Collect(Benchmark_ListRemoveDoubly, Benchmark_ListPrepare, &context,
                          BENCH_MICRO_SAMPLES, s_probe_overhead, &optimized);
        Benchmark_Report("list_remove", "middle", context.count, &reference, &optimized);

        Benchmark_ListPrepare(&context);
        Benchmark_ListRemoveDoubly(&context);
        if ((Benchmark_CountList(&s_delay_list_a) != (context.count - 1U)) ||
            (Benchmark_ValidateDelayList(&s_delay_list_a) == FALSE))
        {
            return FALSE;
        }
        Benchmark_ListPrepare(&context);
        Benchmark_ListRemoveLinear(&context);
        if (((context.count == 1U) && (s_single_head != NULL)) ||
            ((context.count > 1U) && (s_single_head == NULL)))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static uint8_t Benchmark_RunDelayInsertTests(void)
{
    static const uint32_t counts[] = {4U, 16U, 32U, 64U};
    static const char *const scenarios[] = {"head", "middle", "tail", "equal"};
    Benchmark_DelayInsertContext context;
    Benchmark_Stats reference;
    Benchmark_Stats optimized;
    uint32_t i;
    uint32_t scenario;

    for (i = 0U; i < (sizeof(counts) / sizeof(counts[0])); ++i)
    {
        for (scenario = 0U; scenario < 4U; ++scenario)
        {
            context.count = counts[i];
            if (scenario == 0U)
            {
                context.insert_ticks = BENCH_DELAY_INTERVAL / 2U;
            }
            else if (scenario == 1U)
            {
                context.insert_ticks = ((context.count / 2U) * BENCH_DELAY_INTERVAL) + (BENCH_DELAY_INTERVAL / 2U);
            }
            else if (scenario == 2U)
            {
                context.insert_ticks = ((context.count + 1U) * BENCH_DELAY_INTERVAL) + (BENCH_DELAY_INTERVAL / 2U);
            }
            else
            {
                context.insert_ticks = (context.count / 2U) * BENCH_DELAY_INTERVAL;
            }

            Benchmark_Collect(Benchmark_DelayInsertReference, Benchmark_DelayInsertPrepare, &context,
                              BENCH_MICRO_SAMPLES, s_probe_overhead, &reference);
            Benchmark_Collect(Benchmark_DelayInsertDelta, Benchmark_DelayInsertPrepare, &context,
                              BENCH_MICRO_SAMPLES, s_probe_overhead, &optimized);
            Benchmark_Report("delay_insert", scenarios[scenario], context.count, &reference, &optimized);

            Benchmark_DelayInsertPrepare(&context);
            Benchmark_DelayInsertDelta(&context);
            if (Benchmark_ValidateDelayInsertion(&context) == FALSE)
            {
                return FALSE;
            }
        }
    }

    return TRUE;
}

static uint8_t Benchmark_RunDelayTickTests(void)
{
    static const uint32_t counts[] = {1U, 4U, 16U, 32U, 64U};
    static const char *const scenarios[] = {"none_due", "one_due", "burst_due"};
    Benchmark_TickContext context;
    Benchmark_Stats reference;
    Benchmark_Stats optimized;
    uint32_t i;
    uint32_t scenario;

    for (i = 0U; i < (sizeof(counts) / sizeof(counts[0])); ++i)
    {
        for (scenario = 0U; scenario < 3U; ++scenario)
        {
            context.count = counts[i];
            context.scenario = (Benchmark_TickScenario)scenario;
            Benchmark_Collect(Benchmark_DelayTickReference, Benchmark_TickPrepare, &context,
                              BENCH_MICRO_SAMPLES, s_probe_overhead, &reference);
            Benchmark_Collect(Benchmark_DelayTickDelta, Benchmark_TickPrepare, &context,
                              BENCH_MICRO_SAMPLES, s_probe_overhead, &optimized);
            Benchmark_Report("delay_tick", scenarios[scenario], context.count, &reference, &optimized);

            Benchmark_TickPrepare(&context);
            Benchmark_DelayTickDelta(&context);
            Benchmark_DelayTickReference(&context);
            if (Benchmark_ValidateTick(&context) == FALSE)
            {
                return FALSE;
            }
        }
    }

    return TRUE;
}

static uint8_t Benchmark_RunWorkloadTests(void)
{
    static const uint32_t counts[] = {8U, 16U, 32U, 64U};
    static const char *const profiles[] = {"sparse", "dense", "burst"};
    Benchmark_WorkloadContext context;
    Benchmark_Stats reference;
    Benchmark_Stats optimized;
    uint32_t i;
    uint32_t profile;

    for (i = 0U; i < (sizeof(counts) / sizeof(counts[0])); ++i)
    {
        for (profile = 0U; profile < 3U; ++profile)
        {
            context.count = counts[i];
            context.ticks = BENCH_WORKLOAD_TICKS;
            context.profile = (Benchmark_WorkloadProfile)profile;
            Benchmark_Collect(Benchmark_WorkloadLinear, Benchmark_WorkloadPrepare, &context,
                              BENCH_WORKLOAD_SAMPLES, s_probe_overhead, &reference);
            Benchmark_Collect(Benchmark_WorkloadDelta, Benchmark_WorkloadPrepare, &context,
                              BENCH_WORKLOAD_SAMPLES, s_probe_overhead, &optimized);
            Benchmark_Report("delay_workload", profiles[profile], context.count, &reference, &optimized);

            Benchmark_WorkloadPrepare(&context);
            Benchmark_WorkloadDelta(&context);
            Benchmark_WorkloadLinear(&context);
            if (Benchmark_ValidateWorkload(&context) == FALSE)
            {
                return FALSE;
            }
        }
    }

    return TRUE;
}

static uint8_t Benchmark_RunPoolTests(void)
{
    static const uint32_t counts[] = {4U, 16U, 32U, 64U};
    Benchmark_PoolContext context;
    Benchmark_Stats reference;
    Benchmark_Stats optimized;
    uint32_t i;

    for (i = 0U; i < (sizeof(counts) / sizeof(counts[0])); ++i)
    {
        context.count = counts[i];
        Benchmark_Collect(Benchmark_PoolLinear, Benchmark_PoolPrepare, &context,
                          BENCH_MICRO_SAMPLES, s_probe_overhead, &reference);
        Benchmark_Collect(Benchmark_PoolFreeList, Benchmark_PoolPrepare, &context,
                          BENCH_MICRO_SAMPLES, s_probe_overhead, &optimized);
        Benchmark_Report("mem_pool_pair", "get_put", context.count, &reference, &optimized);

        Benchmark_PoolPrepare(&context);
        Benchmark_PoolFreeList(&context);
        if (s_pool.FreeBlocks != 1U)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static uint8_t Benchmark_RunQueueTests(void)
{
    static const uint32_t capacities[] = {4U, 16U, 32U, 64U};
    Benchmark_QueueContext context;
    Benchmark_Stats reference;
    Benchmark_Stats optimized;
    uint32_t i;

    for (i = 0U; i < (sizeof(capacities) / sizeof(capacities[0])); ++i)
    {
        context.capacity = capacities[i];
        Benchmark_Collect(Benchmark_QueueLinear, Benchmark_QueuePrepare, &context,
                          BENCH_MICRO_SAMPLES, s_probe_overhead, &reference);
        Benchmark_Collect(Benchmark_QueueRing, Benchmark_QueuePrepare, &context,
                          BENCH_MICRO_SAMPLES, s_probe_overhead, &optimized);
        Benchmark_Report("queue_pair", "shift_vs_ring", context.capacity, &reference, &optimized);

        Benchmark_QueuePrepare(&context);
        Benchmark_QueueRing(&context);
        if (s_queue.MsgCount != (context.capacity - 1U))
        {
            return FALSE;
        }
    }

    return TRUE;
}

void Benchmark_RuntimeRecordKernelTick(uint32_t cycles)
{
    uint32_t adjusted_cycles;

    if (g_BenchmarkRuntimeActive == FALSE)
    {
        return;
    }

    adjusted_cycles = (cycles > s_runtime_tick_probe) ? (cycles - s_runtime_tick_probe) : 0U;
    if (s_runtime_tick_count == 0U || adjusted_cycles < s_runtime_tick_min)
    {
        s_runtime_tick_min = adjusted_cycles;
    }
    if (adjusted_cycles > s_runtime_tick_max)
    {
        s_runtime_tick_max = adjusted_cycles;
    }
    s_runtime_tick_total += adjusted_cycles;
    ++s_runtime_tick_count;
}

static void Benchmark_RuntimeWorkerTask(void *param)
{
    Benchmark_RuntimeWorker *worker = (Benchmark_RuntimeWorker *)param;

    for (;;)
    {
        ++worker->wake_count;
        OS_Delay(worker->period);
    }
}

static void Benchmark_RuntimeControlTask(void *param)
{
    uint32_t start_tick;
    uint32_t average_cycles;
    uint32_t i;

    (void)param;
    g_BenchmarkRuntimeActive = FALSE;
    s_runtime_tick_count = 0U;
    s_runtime_tick_total = 0U;
    s_runtime_tick_min = 0U;
    s_runtime_tick_max = 0U;
    for (i = 0U; i < BENCH_RUNTIME_WORKERS; ++i)
    {
        s_runtime_workers[i].wake_count = 0U;
    }

    printf("BENCHMARK,RUNTIME_START,ticks=%lu,workers=%u\r\n",
           (unsigned long)BENCH_RUNTIME_TICKS,
           (unsigned int)BENCH_RUNTIME_WORKERS);

    start_tick = g_SystemTickCount;
    if (s_dwt_available != FALSE)
    {
        g_BenchmarkRuntimeActive = TRUE;
    }

    while ((uint32_t)(g_SystemTickCount - start_tick) < BENCH_RUNTIME_TICKS)
    {
        OS_Delay(1U);
    }

    g_BenchmarkRuntimeActive = FALSE;
    average_cycles = (s_runtime_tick_count != 0U) ?
                         (s_runtime_tick_total / s_runtime_tick_count) :
                         0U;
    printf("BENCH,runtime_kernel_tick,samples=%lu,min=%lu,avg=%lu,max=%lu,probe=%lu\r\n",
           (unsigned long)s_runtime_tick_count,
           (unsigned long)s_runtime_tick_min,
           (unsigned long)average_cycles,
           (unsigned long)s_runtime_tick_max,
           (unsigned long)s_runtime_tick_probe);

    for (i = 0U; i < BENCH_RUNTIME_WORKERS; ++i)
    {
        printf("BENCH,runtime_worker,index=%lu,period=%lu,wakes=%lu\r\n",
               (unsigned long)i,
               (unsigned long)s_runtime_workers[i].period,
               (unsigned long)s_runtime_workers[i].wake_count);
    }

    printf("BENCHMARK,TEST_COMPLETE\r\n");

    for (;;)
    {
        OS_Delay(0x7FFFFFFFUL);
    }
}

void Benchmark_RuntimeCreateTasks(void)
{
    static const uint32_t periods[BENCH_RUNTIME_WORKERS] = {3U, 5U, 7U, 11U, 13U, 17U, 19U, 23U};
    uint32_t i;

    g_BenchmarkRuntimeActive = FALSE;
    for (i = 0U; i < BENCH_RUNTIME_WORKERS; ++i)
    {
        s_runtime_workers[i].period = periods[i];
        s_runtime_workers[i].wake_count = 0U;
        if (OS_TaskCreate(&s_runtime_workers[i].tcb,
                          Benchmark_RuntimeWorkerTask,
                          &s_runtime_workers[i],
                          s_runtime_workers[i].stack,
                          BENCH_RUNTIME_WORKER_STACK_WORDS,
                          (uint8_t)(i + 1U)) != OS_OK)
        {
            printf("BENCH,ERROR,RUNTIME_WORKER_CREATE,index=%lu\r\n", (unsigned long)i);
        }
    }

    if (OS_TaskCreate(&s_runtime_control_tcb,
                      Benchmark_RuntimeControlTask,
                      NULL,
                      s_runtime_control_stack,
                      BENCH_RUNTIME_CONTROL_STACK_WORDS,
                      0U) != OS_OK)
    {
        printf("BENCH,ERROR,RUNTIME_CONTROL_CREATE\r\n");
        printf("BENCHMARK,TEST_COMPLETE\r\n");
    }
}

void Benchmark_RunAll(void)
{
    uint8_t passed = TRUE;

    SystemCoreClockUpdate();
    s_dwt_available = Benchmark_DWTInit();
    if (s_dwt_available == FALSE)
    {
        printf("BENCH,ERROR,DWT_CYCCNT_UNAVAILABLE\r\n");
        printf("BENCHMARK,STRUCTURAL_UNAVAILABLE\r\n");
        return;
    }

    s_probe_overhead = Benchmark_CalibrateProbe();
    s_runtime_tick_probe = Benchmark_CalibrateRuntimeTickProbe();
    printf("\r\nBENCHMARK,DWT_CYCCNT,HCLK=%luHz,probe_min=%lu,micro_samples=%u,workload_samples=%u,workload_ticks=%lu\r\n",
           (unsigned long)SystemCoreClock,
           (unsigned long)s_probe_overhead,
           (unsigned int)BENCH_MICRO_SAMPLES,
           (unsigned int)BENCH_WORKLOAD_SAMPLES,
           (unsigned long)BENCH_WORKLOAD_TICKS);
    printf("BENCH,name,scenario,N,ref_*,opt_*,saved_p50,speedup\r\n");

    passed &= Benchmark_RunPriorityTests();
    passed &= Benchmark_RunListTests();
    passed &= Benchmark_RunDelayInsertTests();
    passed &= Benchmark_RunDelayTickTests();
    passed &= Benchmark_RunWorkloadTests();
    passed &= Benchmark_RunPoolTests();
    passed &= Benchmark_RunQueueTests();

    printf("BENCHMARK,STRUCTURAL_%s\r\n", (passed != FALSE) ? "PASS" : "FAIL");
}

#else

uint8_t Benchmark_IsDWTAvailable(void)
{
    return 0U;
}

void Benchmark_RunAll(void)
{
}

void Benchmark_RuntimeCreateTasks(void)
{
}

void Benchmark_RuntimeRecordKernelTick(uint32_t cycles)
{
    (void)cycles;
}

#endif /* OS_BENCHMARK_ENABLE */

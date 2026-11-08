/**
 ******************************************************************************
 * @file    test_main.c
 * @brief   主机测试：内核链表、延时差值链表、就绪位图、选择下一任务
 *
 * 编译方式（见 Makefile）：直接把 rtos/Src/os_core.c 编进来，配合 host_stubs.c 提供的桩函数。
 * 只测试可隔离的纯 C 逻辑；异常、栈帧、真实上下文切换和中断屏蔽由 QEMU/GDB 测试负责。
 *
 * 断言处理：内核的 OS_ASSERT 失败会调用 OS_AssertFailed（原实现是死循环）。
 * Makefile 强制包含 host_hooks.h 重定向 OS_ASSERT，本文件提供
 * host_assert_failed，用 longjmp 跳出，使“应触发断言”的用例可以检查。
 *
 * 每个用例按 HOST-xx 编号；输出 "CASE <编号> PASS|FAIL <说明>"，最后 "HOSTRESULT PASS|FAIL"。
 * 退出码：0 全部通过；1 有失败。
 ******************************************************************************
 */
#include "os_core.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 测试框架 ---- */

static int g_total = 0;   /**< 已执行的检查数 */
static int g_failed = 0;  /**< 失败的检查数 */

static int g_assert_hits = 0;       /**< 内核断言被触发的次数 */
static jmp_buf g_assert_jmp;        /**< 断言触发后的跳回点 */
static int g_assert_armed = 0;      /**< 是否允许断言跳回（只有预期会断言的用例才布防） */

/**
 * @brief  替代内核 OS_AssertFailed：记录并（若已布防）跳回测试代码
 * @param  file 触发断言的源文件
 * @param  line 触发断言的行号
 * @note   未布防时断言意味着内核在不该断言的地方断言了，直接判测试失败并退出。
 */
void host_assert_failed(const char *file, int line)
{
    g_assert_hits++;
    if (g_assert_armed)
        longjmp(g_assert_jmp, 1);
    printf("CASE UNEXPECTED_ASSERT FAIL %s:%d\n", file, line);
    printf("HOSTRESULT FAIL\n");
    exit(1);
}

/**
 * @brief  检查一个条件并输出结果
 * @param  id   用例编号，如 "HOST-01"
 * @param  cond 条件是否成立
 * @param  what 对检查内容的说明
 */
static void check(const char *id, int cond, const char *what)
{
    g_total++;
    if (!cond)
        g_failed++;
    printf("CASE %s %s %s\n", id, cond ? "PASS" : "FAIL", what);
}

/** @brief 重置内核全局状态：清空就绪表、延时表和位图，供每个用例独立运行 */
static void reset_kernel(void)
{
    for (int i = 0; i < OS_MAX_PRIO; i++)
        List_Init(&ReadyList[i]);
    List_Init(&DelayList);
    g_PrioMap = 0;
    CurrentTCB = NULL;
    NextTCB = NULL;
    g_OSRunning = FALSE;
    g_host_schedule_calls = 0;
    g_assert_hits = 0;
}

/**
 * @brief 构造一个只设置优先级的 TCB
 * @param t    要初始化的 TCB
 * @param prio 优先级（数值越小优先级越高）
 */
static void make_tcb(OS_TCB *t, uint8_t prio)
{
    memset(t, 0, sizeof(*t));
    t->Priority = prio;
    t->OriginalPrio = prio;
    t->State = TASK_READY;
}

/**
 * @brief  按头→尾顺序检查链表是否与期望数组一致，同时校验每个节点的 Prev 和链表 Tail
 * @param  l      被检查的链表
 * @param  expect 期望的节点指针数组
 * @param  n      期望的节点数
 * @return 1 一致；0 不一致
 */
static int list_matches(OS_List *l, OS_TCB **expect, int n)
{
    OS_TCB *prev = NULL, *it = l->Head;
    for (int i = 0; i < n; i++)
    {
        if (it != expect[i] || it->Prev != prev)
            return 0;
        prev = it;
        it = it->Next;
    }
    return it == NULL && l->Tail == prev;
}

/* ---- 用例 ---- */

/** @brief HOST-50~54：在桩调度器下检查阻塞/唤醒的状态、节点、位图与 FIFO。 */
static void test_wait_state(void)
{
    OS_List wait;
    OS_TCB a, b;
    reset_kernel();
    List_Init(&wait);
    make_tcb(&a, 2);
    make_tcb(&b, 5);
    OS_ReadyListAdd(&a);
    OS_ReadyListAdd(&b);
    CurrentTCB = &a;
    OS_TaskSuspend(&wait);
    check("HOST-50", a.State == TASK_READY && wait.Head == NULL &&
          g_host_schedule_calls == 0, "调度器未启动时内部挂起函数不修改状态");
    g_OSRunning = TRUE;
    OS_TaskSuspend(&wait);
    check("HOST-51", a.State == TASK_BLOCKED && wait.Head == &a && wait.Tail == &a &&
          ReadyList[2].Head == NULL && g_PrioMap == (1U << 5) &&
          NextTCB == &b && g_host_schedule_calls == 1,
          "阻塞后从就绪表移入等待表，位图和下一任务一致");
    OS_TCB c;
    make_tcb(&c, 1);
    c.State = TASK_BLOCKED;
    List_InsertTail(&wait, &c);
    check("HOST-52", OS_TaskResume(&wait) == &a && a.State == TASK_READY &&
          ReadyList[2].Head == &a && wait.Head == &c && c.Prev == NULL &&
          g_PrioMap == ((1U << 2) | (1U << 5)),
          "唤醒按 FIFO 选择 a，状态和两条链表保持一致");
    check("HOST-53", OS_TaskResume(&wait) == &c && c.State == TASK_READY &&
          wait.Head == NULL && wait.Tail == NULL && FindNextTask() == &c,
          "再次唤醒后高优先级 c 可被选择");
    check("HOST-54", OS_TaskResume(&wait) == NULL && g_host_schedule_calls == 1,
          "空等待表唤醒返回 NULL，Resume 本身不请求调度");
    reset_kernel();
}

/** @brief HOST-01~06：普通链表的初始化、插入、删除、弹出 */
static void test_list(void)
{
    OS_List l;
    OS_TCB a, b, c;
    make_tcb(&a, 1);
    make_tcb(&b, 1);
    make_tcb(&c, 1);

    List_Init(&l);
    check("HOST-01", l.Head == NULL && l.Tail == NULL, "空链表头尾均为 NULL");

    List_InsertTail(&l, &a);
    List_InsertTail(&l, &b);
    List_InsertTail(&l, &c);
    OS_TCB *e1[] = { &a, &b, &c };
    check("HOST-02", list_matches(&l, e1, 3), "尾插三个节点后顺序为 a,b,c 且 Prev/Tail 正确");

    List_Remove(&l, &b);
    OS_TCB *e2[] = { &a, &c };
    check("HOST-03", list_matches(&l, e2, 2) && b.Prev == NULL && b.Next == NULL,
          "删除中间节点后为 a,c，被删节点指针清空");

    OS_TCB *p = List_PopHead(&l);
    OS_TCB *e3[] = { &c };
    check("HOST-04", p == &a && list_matches(&l, e3, 1), "PopHead 返回头节点，剩余 c");

    p = List_PopHead(&l);
    check("HOST-05", p == &c && l.Head == NULL && l.Tail == NULL, "弹出最后一个节点后链表为空");
    check("HOST-06", List_PopHead(&l) == NULL, "对空链表 PopHead 返回 NULL");
}

/** @brief HOST-10~15：延时链表的差值编码、同到期时间、推进 */
static void test_delay_list(void)
{
    OS_List l;
    OS_TCB a, b, c, d;
    make_tcb(&a, 1);
    make_tcb(&b, 1);
    make_tcb(&c, 1);
    make_tcb(&d, 1);
    List_Init(&l);

    /* 依次插入 5、2、8：期望链表 b(2) a(3) c(3)，DelayTicks 为相对前一节点的差值 */
    OS_DelayListInsert(&l, &a, 5);
    OS_DelayListInsert(&l, &b, 2);
    OS_DelayListInsert(&l, &c, 8);
    OS_TCB *e1[] = { &b, &a, &c };
    check("HOST-10", list_matches(&l, e1, 3), "按到期时间排序：b,a,c");
    check("HOST-11", b.DelayTicks == 2 && a.DelayTicks == 3 && c.DelayTicks == 3,
          "DelayTicks 为相对前一节点的差值（2,3,3）");

    /* 相同到期时间：插在已有相同值的后面，差值为 0 */
    OS_DelayListInsert(&l, &d, 5);
    OS_TCB *e2[] = { &b, &a, &d, &c };
    check("HOST-12", list_matches(&l, e2, 4) && d.DelayTicks == 0 && c.DelayTicks == 3,
          "同到期时间插在后面，差值为 0，后继差值不变");

    /* 推进：只有链表头递减 */
    OS_DelayListAdvance(&l);
    check("HOST-13", b.DelayTicks == 1 && a.DelayTicks == 3, "Advance 只让链表头递减");
    OS_DelayListAdvance(&l);
    OS_DelayListAdvance(&l);
    check("HOST-14", b.DelayTicks == 0, "头节点递减到 0 后停止（不会下溢）");

    OS_List empty;
    List_Init(&empty);
    OS_DelayListAdvance(&empty);
    check("HOST-15", empty.Head == NULL, "空链表 Advance 无副作用");
}

/** @brief HOST-20~24：就绪表与优先级位图 */
static void test_ready_map(void)
{
    OS_TCB a, b, c;
    reset_kernel();
    make_tcb(&a, 3);
    make_tcb(&b, 3);
    make_tcb(&c, 5);

    OS_ReadyListAdd(&a);
    check("HOST-20", g_PrioMap == (1U << 3), "加入优先级 3 的任务后位图 bit3 置位");
    OS_ReadyListAdd(&b);
    OS_ReadyListAdd(&c);
    check("HOST-21", g_PrioMap == ((1U << 3) | (1U << 5)),
          "再加入优先级 5 后位图 bit3、bit5 置位");

    OS_ReadyListRemove(&a);
    check("HOST-22", g_PrioMap == ((1U << 3) | (1U << 5)), "同优先级还有任务时位图 bit3 保持");
    OS_ReadyListRemove(&b);
    check("HOST-23", g_PrioMap == (1U << 5), "同优先级清空后 bit3 清除");
    OS_ReadyListRemove(&c);
    check("HOST-24", g_PrioMap == 0, "全部移除后位图为 0");
}

/** @brief HOST-30~33：选择下一任务（数值越小优先级越高；同优先级取链表头） */
static void test_find_next(void)
{
    OS_TCB lo1, lo2, hi;
    reset_kernel();
    make_tcb(&lo1, 7);
    make_tcb(&lo2, 7);
    make_tcb(&hi, 2);

    OS_ReadyListAdd(&lo1);
    OS_ReadyListAdd(&lo2);
    check("HOST-30", FindNextTask() == &lo1, "只有优先级 7 时，取 FIFO 队首 lo1");

    OS_ReadyListAdd(&hi);
    check("HOST-31", FindNextTask() == &hi, "高优先级任务就绪后被选中");

    OS_ReadyListRemove(&hi);
    check("HOST-32", FindNextTask() == &lo1, "高优先级任务离开后回到 lo1");

    /* 位图为 0 时内核有 OS_ASSERT(g_PrioMap != 0)，应当触发断言 */
    reset_kernel();
    g_assert_armed = 1;
    if (setjmp(g_assert_jmp) == 0)
        (void)FindNextTask();
    g_assert_armed = 0;
    check("HOST-33", g_assert_hits == 1, "没有就绪任务时 FindNextTask 触发断言");
}

/** @brief HOST-40：同优先级时间片轮转所用的“移到队尾”操作（与 OS_Tick_Handler 做法一致） */
static void test_round_robin(void)
{
    OS_TCB a, b, c;
    reset_kernel();
    make_tcb(&a, 4);
    make_tcb(&b, 4);
    make_tcb(&c, 4);
    OS_ReadyListAdd(&a);
    OS_ReadyListAdd(&b);
    OS_ReadyListAdd(&c);

    OS_List *ls = &ReadyList[4];
    List_Remove(ls, &a);
    List_InsertTail(ls, &a);
    OS_TCB *e[] = { &b, &c, &a };
    check("HOST-40", list_matches(ls, e, 3) && FindNextTask() == &b,
          "头节点移到队尾后下一个是 b");
}

/**
 * @brief  测试入口：依次运行所有用例并汇总
 * @return 0 全部通过；1 有失败或一个检查都没跑
 */
int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--negative-check") == 0)
        check("HOSTNEG-01", 0, "故意失败：检查汇总必须返回 1");
    else if (argc == 2 && strcmp(argv[1], "--negative-assert") == 0)
        List_Init(NULL); /* 未布防的内核断言必须终止测试并返回 1。 */
    else if (argc != 1)
        return 2;
    test_list();
    test_delay_list();
    test_ready_map();
    test_find_next();
    test_round_robin();
    test_wait_state();

    printf("HOSTSUMMARY total=%d failed=%d\n", g_total, g_failed);
    /* 防止“一个检查都没跑”也算通过 */
    if (g_total == 0 || g_failed != 0)
    {
        printf("HOSTRESULT FAIL\n");
        return 1;
    }
    printf("HOSTRESULT PASS\n");
    return 0;
}

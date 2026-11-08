/** @file test_events.h @brief 小型确定性事件记录；调度期间只写内存，结束后打印。 */
#ifndef TEST_EVENTS_H
#define TEST_EVENTS_H
#include "os_core.h"
#include "test_support.h"
#include <stdio.h>

static volatile unsigned event_count;
static struct { uint32_t id, tick; } events[16];

static void event_record(uint32_t id)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    unsigned n = event_count;
    if (n < 16)
    {
        events[n].id = id;
        events[n].tick = g_SystemTickCount;
    }
    event_count = n + 1;
    __set_PRIMASK(mask);
}

static void event_verify(const uint32_t *expected, unsigned count)
{
    /* 测试已经到达终点，冻结事件；不在任务切换关键路径中打印。 */
    __disable_irq();
    SysTick->CTRL = 0;
    test_check(event_count == count, "event_count");
    for (unsigned i = 0; i < event_count && i < 16; i++)
    {
        printf("EVENT %lu tick=%lu\n", (unsigned long)events[i].id,
               (unsigned long)events[i].tick);
        test_check(i < count && events[i].id == expected[i], "event_order");
    }
}
#endif

/** @file neg_event_order.c @brief NEG-09：事件顺序错误必须被共用检查拒绝。 */
#include "test_events.h"
int main(void)
{
    test_begin("NEG-09");
    event_record(2);
    event_record(1);
    const uint32_t expected[] = {1, 2};
    event_verify(expected, 2);
    test_finish();
}

/** @file neg_yield_isr_thread.c @brief ISRNEG-01：线程调用 ISR 尾部入口，必须原生断言。 */
#include "os_core.h"
#include "test_support.h"
/** @brief 故意误用，不允许正常完成。 @return 不返回。 @note 运行器预期失败退出 1。 */
int main(void)
{
    test_begin("ISRNEG-01"); OS_Init(); OS_YieldFromISR(TRUE); test_finish();
}

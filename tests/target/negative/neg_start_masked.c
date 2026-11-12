/** @file neg_start_masked.c @brief STARTNEG-02：调用者已屏蔽时启动必须断言，不能静默开中断或卡死。 */
#include "os_core.h"
#include "test_support.h"
/** @brief 在 PRIMASK=1 的线程调用 Start。 @return 不返回。 @note 运行器要求识别原生断言。 */
int main(void)
{
    test_begin("STARTNEG-02"); OS_Init(); __disable_irq(); OS_StartScheduler(); test_finish();
}

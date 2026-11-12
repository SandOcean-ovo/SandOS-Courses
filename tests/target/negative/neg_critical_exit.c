/**
 * @file neg_critical_exit.c
 * @brief CRITNEG-01：没有进入就退出，必须由原生断言报告失败。
 * @note 测试运行器预期退出 1；不能将断言当正常功能通过。
 */
#include "os_core.h"
#include "test_support.h"
/** @brief 触发未配对退出。 @return 不返回。 @note 保留生产断言。 */
int main(void)
{
    test_begin("CRITNEG-01"); OS_Init();
    OS_ExitCritical();
    test_finish();
}

/** @file host_hooks.h
 * @brief 仅供主机测试：重定向断言宏，并声明被测内部函数。
 * 先加载真实头文件，再替换宏；内核原 OS_AssertFailed 定义保持不变。
 */
#ifndef HOST_HOOKS_H
#define HOST_HOOKS_H
#include "os_core.h"
void host_assert_failed(const char *file, int line);
#undef OS_ASSERT
#define OS_ASSERT(x) do { if (!(x)) host_assert_failed(__FILE__, __LINE__); } while (0)

void OS_ReadyListAdd(OS_TCB *tcb);
void OS_ReadyListRemove(OS_TCB *tcb);
OS_TCB *FindNextTask(void);
void OS_TaskSuspend(OS_List *wait_list);
OS_TCB *OS_TaskResume(OS_List *wait_list);
extern volatile uint32_t g_host_schedule_calls;
#endif

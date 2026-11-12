/**
 ******************************************************************************
 * @file    host_stubs.c
 * @brief   主机测试用的移植层桩函数
 *
 * os_core.c 通过 os_cpu.h 声明的这些函数访问硬件。在主机上它们都是空操作
 * 或纯软件实现，并提供计数器，方便测试检查“是否请求过调度”。
 ******************************************************************************
 */
#include "os_core.h"

/** 被请求调度的次数（OS_Schedule 在真机上是置位 PendSV） */
volatile uint32_t g_host_schedule_calls = 0;

/**
 * @brief  桩：任务栈初始化。主机测试不运行任务，只返回栈顶附近的指针
 * @return 数组末尾指针；不构造或验证 Cortex-M 栈帧，也不承诺对齐
 */
uint32_t *OS_StackInit(void *task_function, void *task_param,
                       uint32_t *stack_init_address, uint32_t stack_depth)
{
    (void)task_function;
    (void)task_param;
    return stack_init_address + stack_depth;
}

/** @brief 桩：不配置定时器 */
void OS_Init_Timer(uint32_t ms) { (void)ms; }

/** @brief 桩：只记录被请求调度的次数 */
void OS_Schedule(void) { g_host_schedule_calls++; }

/** @brief 桩：主机上没有中断可开 */
void OS_Enable_IRQ(void) {}

/** @brief 桩：主机上没有中断可关 */
void OS_Disable_IRQ(void) {}

/**
 * @brief  取位图中最低置位的位号，即最高优先级（数值最小）
 * @param  PrioMap 非零位图
 * @return 最低置位的位号；与真机的 CLZ(RBIT(x)) 等价
 */
uint8_t OS_GetTopPrio(uint32_t PrioMap)
{
    return (uint8_t)__builtin_ctz(PrioMap);
}

/**
 * @brief 主机纯 C 逻辑测试只模拟合法线程，不冒充异常寄存器验证。
 * @return 固定为 0。
 * @note 真实 ISR 与屏蔽寄存器的拒绝路径由 QEMU 用例负责。
 */
uint32_t OS_GetExceptionNumber(void) { return 0U; }

/**
 * @brief 主机测试默认未屏蔽调度。
 * @return 固定为 0。
 * @note 不用此桩证明 PRIMASK／BASEPRI／FAULTMASK 的硬件行为。
 */
uint32_t OS_IsSchedulingMasked(void) { return 0U; }

/**
 ******************************************************************************
 * @file    os_cpu.h
 * @author  SandOcean
 * @version V1.0
 * @date    2025-12-14
 * @brief   RTOS 架构相关头文件 (ARM Cortex-M3)
 *
 * 本文件包含与特定硬件架构相关的定义和宏：
 * - 处理器特定的数据类型
 * - 临界区保护宏 (关中断/开中断)
 * - 堆栈增长方向定义
 * - 汇编指令封装
 *
 ******************************************************************************
 */

#ifndef __OS_CPU_H
#define __OS_CPU_H

#include "os_common.h"
/* 板级头文件：由各 ports/<板>/ 目录提供，构建时用 -I 选择。
 * 它负责提供 CMSIS Core（含 IRQn_Type、SysTick、NVIC、SCB）以及 OS_CPU_CLOCK_HZ，
 * 使通用内核和本移植层不再依赖某个厂商的头文件（如 stm32f1xx.h）。 */
#include "os_board.h"

/* 函数声明 ---------------------------------------------------------------- */

/**
 * @brief  初始化任务栈
 * @param  task_function : 任务入口函数地址
 * @param  task_param    : 传递给任务函数的参数（将写入 R0）
 * @param  stack_init_address : 栈数组的起始地址（低地址）
 * @param  stack_depth   : 栈大小（单位：元素个数，不是字节）
 * @return uint32_t*     : 初始化后的栈顶指针 (SP)
 */
uint32_t* OS_StackInit(void* task_function, void* task_param, uint32_t* stack_init_address, uint32_t stack_depth);


/**
 * @brief  初始化SysTick
 * @param  ms: 时间片长度（单位ms） 
 */
void OS_Init_Timer(uint32_t ms);

/**
 * @brief  请求调度（触发上下文切换）
 */
void OS_Schedule(void);

/**
 * @brief  打开全局中断
 */
void OS_Enable_IRQ(void);

/**
 * @brief  关闭全局中断
 */
void OS_Disable_IRQ(void);

/**
 * @brief  获取最高优先级数值
 */
uint8_t OS_GetTopPrio(uint32_t PrioMap);


/**
 * @brief 只读取得当前异常号，用于阻塞 API 的入口约束。
 * @return 0 表示线程模式，非零表示 Handler 模式。
 * @note Cortex-M3 由 IPSR 读取；不改变中断屏蔽状态。
 */
uint32_t OS_GetExceptionNumber(void);

/**
 * @brief 查询调用者是否屏蔽了本端口所需的调度异常。
 * @return PRIMASK、BASEPRI 或 FAULTMASK 任一非零时为 1，否则为 0。
 * @note 本端口 PendSV 处于最低可配置优先级；不是进入／退出临界区操作。
 */
uint32_t OS_IsSchedulingMasked(void);
#endif /* __OS_CPU_H */

/**
 ******************************************************************************
 * @file    os_board.h
 * @brief   QEMU mps2-an385（Cortex-M3）板级头文件
 *
 * 内核移植层（rtos/Portable/ARM_CM3/os_cpu.h）通过包含 "os_board.h" 获得
 * CMSIS Core 所需的全部板级信息，因此通用内核不再依赖 stm32f1xx.h。
 * 每块板（QEMU、STM32F103 等）各自提供一份同名头文件，构建时用 -I 选择。
 *
 * CMSIS 要求在包含 core_cm3.h 之前提供：
 *   1. IRQn_Type    —— 中断号枚举（负数为内核异常，非负数为外设中断）
 *   2. __CM3_REV    —— 内核修订号
 *   3. __MPU_PRESENT —— 是否有 MPU
 *   4. __NVIC_PRIO_BITS —— NVIC 实现的优先级位数
 *   5. __Vendor_SysTickConfig —— 是否由厂商自定义 SysTick 配置
 *
 * 本文件另外提供内核自己的 OS_CPU_CLOCK_HZ，用于把毫秒换算成 SysTick 重载值。
 ******************************************************************************
 */
#ifndef OS_BOARD_H
#define OS_BOARD_H

#include <stdint.h>

/**
 * @brief 中断号枚举（CMSIS 约定）
 * @note  只列出本工程用到的内核异常，加上 mps2-an385 的 UART0 接收中断作为示例。
 *        数值取自 Cortex-M3 架构：异常号 - 16。
 */
typedef enum IRQn
{
    NonMaskableInt_IRQn   = -14,  /**< NMI */
    HardFault_IRQn        = -13,  /**< 硬故障 */
    MemoryManagement_IRQn = -12,  /**< 存储器管理故障（MPU） */
    BusFault_IRQn         = -11,  /**< 总线故障 */
    UsageFault_IRQn       = -10,  /**< 用法故障 */
    SVCall_IRQn           = -5,   /**< 系统服务调用 */
    DebugMonitor_IRQn     = -4,   /**< 调试监视器 */
    PendSV_IRQn           = -2,   /**< 可挂起的系统服务，用于上下文切换 */
    SysTick_IRQn          = -1,   /**< 系统节拍定时器 */
    UARTRX0_IRQn          = 0     /**< UART0 接收中断（预留，当前未使用） */
} IRQn_Type;

/** Cortex-M3 内核修订号 r2p0（QEMU 模拟的版本，仅供 CMSIS 内部条件判断） */
#define __CM3_REV               0x0200U

/** mps2-an385 的 Cortex-M3 不带 MPU */
#define __MPU_PRESENT           0U

/**
 * NVIC 实现的优先级位数。mps2-an385 为 3 位（8 级），STM32F103 为 4 位（16 级）。
 * 注意：CMSIS 的 NVIC_SetPriority 会先左移 (8 - 位数) 再截断到 8 位，
 *       所以 os_cpu.c 里写的 PendSV=15、SysTick=14 在 3 位下会被截断为 7 和 6，
 *       “PendSV 比 SysTick 优先级更低”的相对顺序依然成立，见 docs 中的说明。
 */
#define __NVIC_PRIO_BITS        3U

/** 使用 CMSIS 自带的 SysTick_Config，而不是厂商自定义实现 */
#define __Vendor_SysTickConfig  0U

#include "core_cm3.h"   /* 来自 third_party/cmsis，ARM CMSIS 6.1.0（Apache-2.0） */

/**
 * 内核时钟频率（Hz）。mps2-an385 的 CPU 时钟为 25 MHz。
 * 只用于把 tick 周期换算成 SysTick 重载值；QEMU 中的运行时间不代表真机周期。
 */
#define OS_CPU_CLOCK_HZ         25000000UL

#endif /* OS_BOARD_H */

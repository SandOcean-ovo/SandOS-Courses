/*
 * os_board.h: QEMU mps2-an385 (Cortex-M3) 板级头文件
 * 提供内核移植层所需的 CMSIS Core 配置。内核通用代码不直接包含厂商头文件。
 */
#ifndef OS_BOARD_H
#define OS_BOARD_H

#include <stdint.h>

typedef enum IRQn
{
    NonMaskableInt_IRQn   = -14,
    HardFault_IRQn        = -13,
    MemoryManagement_IRQn = -12,
    BusFault_IRQn         = -11,
    UsageFault_IRQn       = -10,
    SVCall_IRQn           = -5,
    DebugMonitor_IRQn     = -4,
    PendSV_IRQn           = -2,
    SysTick_IRQn          = -1,
    UARTRX0_IRQn          = 0
} IRQn_Type;

#define __CM3_REV               0x0200U
#define __MPU_PRESENT           0U
/* mps2-an385 的 NVIC 实现 3 位优先级；STM32F103 为 4 位。 */
#define __NVIC_PRIO_BITS        3U
#define __Vendor_SysTickConfig  0U

#include "core_cm3.h"

/* mps2-an385 系统时钟 25 MHz（QEMU 中仅用于换算 tick，不代表真机时序）。 */
#define OS_CPU_CLOCK_HZ         25000000UL

#endif /* OS_BOARD_H */

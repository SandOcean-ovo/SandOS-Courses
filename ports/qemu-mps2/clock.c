/**
 * @file clock.c
 * @brief QEMU mps2-an385 板级时钟查询；只返回该模型固定的 CPU 时钟。
 * @note STM32 适配应在时钟初始化／更新完成后读取实际 SystemCoreClock，不能复制此常数。
 */
#include "os_board.h"
/** @brief 返回模型稳定时钟。 @return 25000000 Hz。 @note 不改硬件，不在此处初始化或切换时钟。 */
uint32_t OS_BoardClockHz(void)
{
    return OS_CPU_CLOCK_HZ;
}

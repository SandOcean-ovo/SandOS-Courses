/**
 ******************************************************************************
 * @file    os_cpu.c
 * @author  SandOcean
 * @version V1.0
 * @date    2025-12-14
 * @brief   RTOS 移植层 C 语言实现 (ARM Cortex-M3)
 *
 * 本文件包含涉及硬件细节但可用 C 语言实现的函数：
 * - 任务栈初始化 (Task_Stack_Init)
 * - 伪造异常栈帧 (xPSR, PC, LR, R12, R3-R0)
 *
 ******************************************************************************
 */

#include "os_cpu.h"

void OS_TaskReturn(void)
{
    for (;;)
        ;
}

uint32_t *OS_StackInit(void *task_function, void *task_param, uint32_t *stack_init_address, uint32_t stack_depth)
{
    /* 第一步：找到栈顶 */
    uint32_t *sp = stack_init_address + stack_depth;

    /* 第二步：字节对齐 */
    sp = (uint32_t *)((uint32_t)sp & 0xFFFFFFF8); // 先把sp转成uint32_t，再把最后3位抹成0，最后转成uint32_t *

    /* 第三步：填入数据 */
    /* 硬件区 */
    *(--sp) = (uint32_t)0x01000000;    // xPSR
    *(--sp) = (uint32_t)task_function; // PC
    *(--sp) = (uint32_t)OS_TaskReturn; // LR
    *(--sp) = (uint32_t)0x0;           // R12
    *(--sp) = (uint32_t)0x0;           // R3
    *(--sp) = (uint32_t)0x0;           // R2
    *(--sp) = (uint32_t)0x0;           // R1
    *(--sp) = (uint32_t)task_param;           // R0

    /* 软件区 */
    *(--sp) = (uint32_t)0x0; // R11
    *(--sp) = (uint32_t)0x0; // R10
    *(--sp) = (uint32_t)0x0; // R9
    *(--sp) = (uint32_t)0x0; // R8
    *(--sp) = (uint32_t)0x0; // R7
    *(--sp) = (uint32_t)0x0; // R6
    *(--sp) = (uint32_t)0x0; // R5
    *(--sp) = (uint32_t)0x0; // R4

    /* 第四步：返回sp */
    return sp;
}

void OS_Init_Timer(uint32_t ms)
{
    /* 重载值 = 每毫秒的时钟周期数 * 毫秒数。
     * 时钟频率由板级头文件 os_board.h 的 OS_CPU_CLOCK_HZ 提供，
     * 不再写死 72 MHz（QEMU mps2-an385 为 25 MHz，STM32F103 常用 72 MHz）。
     * 先除后乘避免 clock*ms 在 32 位下溢出；时钟需为 1000 的整数倍，否则有舍入误差。 */
    uint32_t ticks = (OS_CPU_CLOCK_HZ / 1000U) * ms;

    if (SysTick_Config(ticks))
    {
        while (1)
            ; /* 配置失败了，死循环 */
    }

    /* 设置优先级 */
    NVIC_SetPriority(PendSV_IRQn, 15);

    NVIC_SetPriority(SysTick_IRQn, 14);

    __enable_irq(); // 开全局中断
}

void OS_Schedule(void)
{
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void OS_Enable_IRQ(void)
{
    __enable_irq();
}

void OS_Disable_IRQ(void)
{
    __disable_irq();
}

uint8_t OS_GetTopPrio(uint32_t PrioMap)
{
    return __CLZ(__RBIT(PrioMap));
}
/**
 * @brief 取得当前 Cortex-M3 异常号。
 * @return IPSR 的值；线程为 0。
 * @note 依据 CMSIS Core Register Access / __get_IPSR，只读取处理器状态。
 */
uint32_t OS_GetExceptionNumber(void)
{
    return __get_IPSR();
}

/**
 * @brief 查询任何会阻止最低优先级 PendSV 的调用者屏蔽条件。
 * @return 有效 PRIMASK、BASEPRI、FAULTMASK 任一非零时返回 1，否则为 0。
 * @note 不把此查询当作保存／恢复接口；不改变任何屏蔽寄存器。
 */
uint32_t OS_IsSchedulingMasked(void)
{
    return (__get_PRIMASK() != 0U || __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U) ? 1U : 0U;
}

/**
 * @brief 原样保存 PRIMASK 并屏蔽可配置异常。
 * @return 调用前的 PRIMASK。
 * @note 读取与屏蔽之间允许被 ISR 抢占；ISR 必须配对恢复自己的状态，不修改任务计数。
 */
uint32_t OS_SaveIRQ(void)
{
    uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    return saved_mask;
}

/**
 * @brief 恢复相应保护区进入前的 PRIMASK。
 * @param saved_mask 对应 OS_SaveIRQ 返回的原值。
 * @return 无。
 * @note 使用 CMSIS intrinsic 保留编译器的屏蔽操作语义，不使用无条件 enable。
 */
void OS_RestoreIRQ(uint32_t saved_mask)
{
    __set_PRIMASK(saved_mask);
}

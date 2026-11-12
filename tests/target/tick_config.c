/**
 * @file tick_config.c
 * @brief TICK-01：精确换算、周期边界、板级时钟逐次读取及失败不改硬件。
 * @note WRAP=OS_BoardClockHz 注入数值；QEMU 物理模型仍为 25 MHz，边界组始终屏蔽 IRQ。
 */
#include "os_core.h"
#include "test_support.h"
#include <stdio.h>
extern volatile uint32_t g_CriticalNesting;
uint32_t __real_OS_BoardClockHz(void);
static uint32_t fake_hz, reads;

/** @brief 测试板级时钟 getter，每次调用计数。 @return 本组注入的 Hz。 @note 不模拟真实硬件改频，只验证配置计算。 */
uint32_t __wrap_OS_BoardClockHz(void) { reads++; return fake_hz; }
struct Input { uint32_t hz, ms, cycles; }; /* cycles=0 表示必须拒绝。 */
static const struct Input cases[] = {
    {25000000,1,25000}, {72000000,1,72000}, {8000000,1,8000},
    {1500,2,3}, {1001,1000,1001}, {1,2000,2},
    {16777216,1000,16777216}, {8388608,2000,16777216}, {25000000,671,16775000},
    {0,1,0}, {25000000,0,0}, {1001,1,0}, {1,1000,0}, {1,1,0},
    {16777217,1000,0}, {25000000,672,0}, {25000000,171799,0},
    {UINT32_MAX,UINT32_MAX,0}, {UINT32_MAX,1000,0}
};

/** @brief 在 IRQ 屏蔽状态逐项检查成功／失败和完整恢复。 @param in 输入与期望。 @return 无。 @note 不读取 CTRL 的 COUNTFLAG 作为无副作用观测。 */
static void trial(const struct Input *in)
{
    __disable_irq(); __set_BASEPRI(0x80); __set_FAULTMASK(1);
    SysTick->CTRL = 0; SysTick->LOAD = 123; SysTick->VAL = 0;
    NVIC_SetPriority(PendSV_IRQn, 2); NVIC_SetPriority(SysTick_IRQn, 3);
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk | SCB_ICSR_PENDSTSET_Msk;
    uint32_t ctrl_before = SysTick->CTRL & 7;
    printf("INPUT hz=%lu ms=%lu expected_cycles=%lu\n", (unsigned long)in->hz,
           (unsigned long)in->ms, (unsigned long)in->cycles);
    fake_hz = in->hz; reads = 0;
    uint32_t status = OS_Init_Timer(in->ms);
    test_check(status == (in->cycles ? 0U : 1U), "configuration_result");
    test_check(in->cycles ? (SysTick->LOAD == in->cycles - 1 && (SysTick->CTRL & 7) == 7) :
               (SysTick->LOAD == 123 && (SysTick->CTRL & 7) == ctrl_before && SysTick->VAL == 0),
               "exact_reload_or_unchanged_registers");
    uint32_t lowest = (1U << __NVIC_PRIO_BITS) - 1;
    test_check(NVIC_GetPriority(PendSV_IRQn) == (in->cycles ? lowest : 2) &&
               NVIC_GetPriority(SysTick_IRQn) == (in->cycles ? lowest - 1 : 3),
               "priority_configuration_or_preservation");
    test_check(__get_PRIMASK() == 1 && __get_BASEPRI() == 0x80 && __get_FAULTMASK() == 1 &&
               g_CriticalNesting == 0 && g_SystemTickCount == 0, "masks_and_kernel_state_preserved");
    test_check((SCB->ICSR & 0x14000000) == 0x14000000, "pending_requests_not_cleared");
    test_check(reads == 1, "board_clock_read_once_each_call");
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;
    __set_BASEPRI(0); __set_FAULTMASK(0); __enable_irq();
}

/** @brief 覆盖 19 组精确／非法输入及普通开中断入口。 @return 不返回。 @note 最短周期组只验寄存器，真实默认运行由原调度／启动套件覆盖。 */
int main(void)
{
    test_begin("TICK-01"); OS_Init();
    test_check(__real_OS_BoardClockHz() == 25000000, "actual_qemu_board_clock");
    for (unsigned i=0; i < sizeof(cases)/sizeof(cases[0]); i++) trial(&cases[i]);
    fake_hz = 25000000; reads = 0;
    uint32_t status = OS_Init_Timer(1);
    uint32_t restored = __get_PRIMASK();
    SysTick->CTRL = 0; SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    test_check(status == 0 && restored == 0 && reads == 1, "unmasked_caller_remains_unmasked");
    test_finish();
}

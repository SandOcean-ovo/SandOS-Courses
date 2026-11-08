/**
 * @file probe.c
 * @brief 阶段 0 的独立 Keil 仿真能力探针，不链接 SandOS。
 * @note 不使用 C 运行时；复位后显式初始化所有可观察变量。仅测试内核异常。
 */
#define REG32(address) (*(volatile unsigned int *)(address))
volatile unsigned int g_ticks, g_pendsv, g_thread_ipsr, g_tick_ipsr;
volatile unsigned int g_pendsv_ipsr, g_started, g_done, g_memory[4];
extern unsigned int read_ipsr(void);

/** @brief 为调试器提供稳定的完成断点。
 * @return 不返回。@note 仅在测试条件完成后进入，不能用到达 main 代替通过。
 */
__attribute__((noinline)) void probe_finished(void)
{
    for (;;) { __asm { NOP } }
}

/** @brief 记录真实 SysTick 异常并请求低优先级 PendSV。
 * @return 无。@note 第三次关闭 SysTick，使最终计数确定，不依赖调试器速度。
 */
void SysTick_Handler(void)
{
    g_tick_ipsr = read_ipsr();
    ++g_ticks;
    if (g_ticks == 3U) { REG32(0xE000E010U) = 0U; }
    REG32(0xE000ED04U) = 1U << 28;
}

/** @brief 记录挂起后的 PendSV 进入次数及异常号。
 * @return 无。@note 不进行任务切换，不能证明 SandOS 上下文恢复正确。
 */
void PendSV_Handler(void)
{
    g_pendsv_ipsr = read_ipsr();
    ++g_pendsv;
}

/** @brief 初始化观测内存，启动三次 tick 并等待对应 PendSV 完成。
 * @return 不返回。@note 仅使用处理器内部寄存器，不配置 STM32 外设和 PLL。
 */
void probe_main(void)
{
    g_ticks = 0U; g_pendsv = 0U; g_done = 0U;
    g_tick_ipsr = 0U; g_pendsv_ipsr = 0U;
    g_thread_ipsr = read_ipsr();
    g_memory[0] = 0x12345678U; g_memory[1] = 0xA5A5A5A5U;
    g_memory[2] = 0x528U; g_memory[3] = ~g_memory[0];
    g_started = 0xAC500001U;
    /* 显式选择本探针向量表；PendSV 低于 SysTick，避免在 tick 中抢入。 */
    REG32(0xE000ED08U) = 0x08000000U;
    REG32(0xE000ED20U) = 0x80FF0000U;
    REG32(0xE000E014U) = 7999U;
    REG32(0xE000E018U) = 0U;
    REG32(0xE000E010U) = 7U;
    while ((g_ticks < 3U) || (g_pendsv < 3U)) { __asm { NOP } }
    g_done = 0x600D0001U;
    probe_finished();
}

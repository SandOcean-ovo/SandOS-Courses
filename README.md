# SandOS - 轻量级嵌入式实时操作系统

<p align="center">
  <img src="https://img.shields.io/badge/架构-ARM%20Cortex--M3%20%2B%20RISC--V QingKeV4-blue" alt="Supported Architectures">
  <img src="https://img.shields.io/badge/语言-C-blue" alt="Language C">
  <img src="https://img.shields.io/badge/授权-MIT-green" alt="License">
</p>

**SandOS** 是一个专为嵌入式学习与实践设计的轻量级 RTOS 内核，完全从零手写，代码简洁易懂，适合作为学习 RTOS 内部原理的入门项目，或作为嵌入式项目的内核基座。

---

## 硬件支持

| 架构 | 芯片示例 | 说明 |
|------|----------|------|
| **ARM Cortex-M3** | STM32F103 等 | 利用硬件自动压栈，上下文切换高效 |
| **RISC-V QingKe V4** | CH32V203 | 全软件保存上下文，深入理解寄存器操作 |

> 项目已实现双架构适配，通过统一的硬件抽象层接口，同一套内核代码可无缝切换至不同平台。

---

## 核心特性

### 调度器
- **O(1) 优先级查找**：基于位图算法 + 查表法实现，无论任务多少，调度延迟恒定
- **抢占式调度**：高优先级任务可立即抢占低优先级任务，保证实时性
- **32 个优先级**：0 为最高，31 为最低

### 任务管理
- 任务创建、删除、挂起、恢复
- 阻塞延时（支持有序延时链表）
- 栈溢出检测（可选）

### 同步与通信
- **信号量**：计数型，支持资源计数与同步
- **互斥锁**：
  - **单锁直接优先级继承**：等待者可抬高持有者优先级，最终释放时恢复；不支持多锁或传递继承
  - **同锁递归**：深度 1～255，超限返回 `OS_ERR_NESTING`；持锁时申请第二把不同锁返回 `OS_ERR_RESOURCE`，不阻塞且不改状态
  - **交接规则**：高优先级等待者优先、同优先级 FIFO；新持有者先转为就绪状态再入就绪队列。仅在线程且没有用户外层临界区时调用；Pend 已在入口拒绝非法上下文；Post 的调用边界仍由调用者遵守
- **消息队列**：支持结构体数据传输

### 内存管理
- **静态内存池**：固定块大小，无内存碎片化风险
- **O(1) 分配与释放**：时间确定性，适合实时系统

### 时基管理
- 基于 SysTick 的时间片轮转
- 有序延时链表，延时精度高

---

## 目录结构

```
SandOS/
├── rtos/
│   ├── Inc/
│   │   └── os_core.h          # 内核核心头文件
│   ├── Src/
│   │   └── os_core.c          # 内核核心实现
│   └── Portable/
│       ├── os_common.h        # 统一抽象层接口
│       ├── ARM_CM3/           # Cortex-M3 移植
│       │   ├── os_cpu.c
│       │   ├── os_cpu.h
│       │   └── os_cpu_a.s     # 汇编实现
│       └── RISC-V_QingkeV4/   # RISC-V 移植
│           ├── os_cpu.c
│           ├── os_cpu.h
│           └── os_cpu_a.S     # 汇编实现
├── design.md                  # 设计原理详解
└── README.md
```

---

## 快速开始

```c
// 定义任务控制块和栈
OS_TCB TCB_Blink;
uint32_t Stack_Blink[128];

// 任务函数
void Task_Blink(void *param) {
    while (1) {
        LED_Toggle();
        OS_Delay(500);  // 延时 500 tick；默认 1 kHz 时为 500 ms
    }
}

int main(void) {
    OS_Init();  // 1. 初始化内核

    // 2. 创建任务
    OS_TaskCreate(&TCB_Blink, Task_Blink, NULL,
                  Stack_Blink, 128, 5);

    // 3. 启动调度器
    OS_StartScheduler();

    // 正常运行不会到达这里
    while (1);
}
```

---

## 适合谁

- 🔰 **RTOS 初学者**：想了解内核如何工作，而非仅会调用 API
- 🎓 **计算机/嵌入式学生**：面试项目或课程设计
- 🛠️ **嵌入式工程师**：需要轻量级内核作为项目基座

---

## License

MIT License - 欢迎学习、修改和商业使用。

阻塞／调度接口 `OS_Delay`、`OS_SemWait`、`OS_MutexPend`、`OS_QueueReceive`、`OS_MemGet` 仅允许可调度的线程调用。Cortex-M3 入口检查 ISR、用户外层临界区及中断屏蔽状态；状态接口返回 `OS_ERR_ISR`／`OS_ERR_CRITICAL`，取块返回 NULL，void 延时触发断言，均在修改状态之前拒绝。详见阶段 0《M1接口契约》和阶段 2 验证记录；任务临界区已保存／恢复最外层原 PRIMASK，ISR 使用独立 OS_SaveIRQ／OS_RestoreIRQ 配对原语；三个现有 FromISR 已加入独立保护、累计唤醒，并由 ISR 尾部调用 OS_YieldFromISR；真实异常嵌套及非 tick 唤醒已通过 QEMU 专项。启动状态现区分 STOPPED／STARTING／RUNNING，首次上下文恢复完成后才允许 tick 调度；AC5 完整内核与真机仍待验证。

SysTick 配置从板级 `OS_BoardClockHz()` 取时钟，以 64 位乘积精确计算周期；超范围、小数计数或不能产生周期请求的 LOAD=0 配置会被拒绝。默认 Start 配置 1 ms/tick；`OS_Delay` 始终以 tick 为单位。当前时钟实现和边界回归使用 QEMU，STM32 应返回更新后的实际 CPU 时钟，其完整 Keil 工程与真机仍按 M2 验证。

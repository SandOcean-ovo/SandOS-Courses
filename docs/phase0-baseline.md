# 阶段 0 基线与环境记录

记录日期：2026-10-31。本文只记录实际查到的事实；"未验证"项不得当作已通过。

## 1. 源码基线

| 项 | 值 |
|---|---|
| 上游仓库 | <https://github.com/SandOcean-ovo/SandOS> |
| 基线提交 | `25a06bdea89a467ec7e85fe7f766705b58837961`（`feat:benchmark`），与 TODO 问题索引所用提交相同 |
| 获取方式 | `git fetch upstream`（本机 git 配置了代理 `127.0.0.1:7897`，该代理不可用，用 `-c http.proxy= -c https.proxy=` 绕过） |
| 本地标签 | `upstream-baseline-25a06bd` |
| 分支 | `main` = 上游基线（未验证，不视为稳定版）；`dev` = 课程开发分支 |
| 许可 | 上游 `LICENSE` 为 MIT（Copyright 2025 SandOcean），保留 |

上游共 4 个提交，内容含 `rtos/Inc`、`rtos/Src`、`rtos/Portable/{ARM_CM3,RISC-V_QingkeV4}`、`README.md`、`design.md`。

## 2. 历史问题复核（仅静态阅读，未运行）

对照 `25a06bd` 阅读源码，下列问题的代码特征仍然存在。**尚未写失败用例复现**，在阶段 1 建好测试环境前不能称"已复现"。

| 问题 | 位置 | 静态阅读所见 |
|---|---|---|
| 信号量计数未初始化 | `os_core.c` `OS_SemInit` | 只调用 `List_Init`，没有给 `count` 赋值 |
| 临界区恢复 | `os_core.c` `OS_EnterCritical/OS_ExitCritical` | 退出时嵌套归零就无条件 `OS_Enable_IRQ()`，没有保存进入前的屏蔽状态 |
| 首次启动 tick | `os_core.c` `OS_Start`/`OS_Tick_Handler` | `OS_Init_Timer(1)` 先于 `g_OSRunning = 1`，`CurrentTCB` 为 `NULL` 期间的 tick 路径与 `OS_ASSERT(CurrentTCB != NULL)` 的关系需用测试确认 |
| 互斥锁交接 | `OS_MutexUnlock` | 弹出等待者后直接 `OS_ReadyListAdd`，需确认其 `State` 是否被置回就绪 |
| 继承时的就绪表 | `OS_MutexLock` | 仅当 `Owner->State == TASK_READY` 才摘链改优先级，其余只改数值，需构造用例验证 |
| 固定 72 MHz | `ARM_CM3/os_cpu.c:59` | `72000000 * ms / 1000` 写死，QEMU 的时钟不同 |

## 3. 工具现状

| 工具 | 状态 |
|---|---|
| `arm-none-eabi-gcc` | 已安装：GNU Tools for STM32 13.3.rel1，gcc 13.3.1 |
| `arm-none-eabi-gdb` | 已安装：GDB 14.2.90（STM32 版）；另有 `gdb-multiarch` 16.2 |
| 主机 `gcc` | MSYS2 gcc 14.2.0，可用于主机测试 |
| `make` | PATH 中无 `make`，有 `mingw32-make` 4.4.1（`C:/msys64/mingw64/bin`） |
| `qemu-system-arm` | **未安装**，PATH 与常见目录均未找到。阶段 1 前需安装并记录版本 |
| `python` | 3.14.6，可写运行脚本 |
| `cmake` / `ninja` | 存在（STM32CubeCLT 1.18.0），按 TODO 暂不使用 |
| Keil MDK | 安装在 `C:/Keil_v5`，含 `ARMCLANG`（AC6）、`ARMCC`（AC5）、`STM32F1xx_DFP` 等包。`TOOLS.INI` 标注 `TOOL_VARIANT=mdk_std` |
| AC6 能否编译 | **不能**：`armclang --version` 报 `Failed to check out a license ... INVALID LICENSE ID CODE`。这是现状事实，不对其来源或处理方式下结论 |

## 4. 尚未完成的阶段 0 项

| 项 | 状态 | 阻塞/说明 |
|---|---|---|
| Keil 与 AC6 授权条件核实 | 未完成 | 本机许可无效；TODO 引用的官方页面（MDK-Community 面向非商业用途）需按作者制作付费课程的实际情况单独核实并存档。**不能由本记录推定适用** |
| Keil 软件仿真最小实验 | 未完成 | 依赖可用的合法许可 |
| 锁定读者硬件 | 未完成 | 需作者确定板型、调试器、串口模块 |
| 发布与许可边界 | 未完成 | 需作者决定 |
| 邀请试读读者 | 未完成 | 需作者联系；agent 不代发消息 |
| 工时记录 | 未开始 | 需作者填写实际投入，不以 agent 运行时间代替 |

## 5. 本机待决问题

1. 安装 QEMU（`qemu-system-arm`），阶段 1 的前置条件。
2. 全局 git 代理 `127.0.0.1:7897` 当前不可用，影响 `git fetch/push`，是否修改由作者决定。

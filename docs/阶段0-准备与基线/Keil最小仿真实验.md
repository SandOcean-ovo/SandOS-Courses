# 阶段 0：Keil 最小仿真实验

> 2026-11-08 实测。预计约 2～3 小时用于独立探针、运行器及记录；不是作者实际工时。
> 这是环境能力验证，不是 SandOS 完成版工程。实验没有连接或下载真机。

## 1. 本轮结论与边界

Keil μVision 5.42.0.0 + AC5 V5.06 update 5（build 528）+ STM32F1xx_DFP 2.2.0 可构建并完成本探针：启动处理程序执行、3 次 SysTick、3 次 PendSV、线程／异常号、RAM 数据与 PC/SP 观察全部符合预期。构建 0 Error(s)、0 Warning(s)。探针未链接 SandOS，不证明内核功能、上下文切换、外设、PLL 或真机时序正确。

默认无板路线确定为 Keil + AC5 软件仿真，先覆盖处理器核心必修实验；GCC + QEMU 保留作者自动回归和补充路线。M2 必须检查全部必修实验，若有缺项再使用既定 QEMU 备选，不向读者承诺 STM32 外设仿真。

本配置的 RESET 未自动得到 Flash 向量中的 PC/SP。因此初始化脚本从 `0x08000000` 的实际向量读出 SP、PC，并设置 xPSR 的 T 位后，再执行 `Reset_Handler`。这是调试器辅助启动，不是已验证 STM32 的 BOOT 引脚与地址 0 重映射。工程预配置此步骤，读者不用手工输入寄存器；教材讲解时必须区分硬件行为与脚本行为。

## 2. 最短复现路径

课程根目录 PowerShell：

```powershell
pwsh -NoProfile -File tests/scripts/run_keil_probe.ps1
```

脚本创建独立隐藏 Keil 会话，先强制重建再进入软件仿真，每个进程最多 30 秒，只清理本探针的旧日志和旧 AXF，仅停止自己创建的进程。用户已有 Keil 会话不被终止。不安装工具，不变更系统 PATH 或许可，不使用 ST-Link 下载。

预期输出：

```text
ENV-KEIL-01 PASS (startup, 3 SysTick, 3 PendSV, exception numbers, memory, registers)
```

进程退出 0。工具路径不同时使用 `-KeilPath` 指定现有 UV4.exe；本轮仅验证默认路径。运行器原样检查构建诊断、文件是否新生成、调试日志中的值、结束标记、PC/SP 范围与 SP 的 8 字节对齐；调试进程自己退出 0 仍不足以判定成功。

故意失败验证（本轮实际执行）：

```powershell
pwsh -NoProfile -File tests/scripts/run_keil_probe.ps1 -ExpectedTicks 4
```

实际退出 1，输出 `ENV-KEIL-01 FAIL: Missing or unexpected exception results`。目标仍产生 3 次 tick，而判定要求 4，证明运行器检查实际结果。超时不是这个负例的成功依据。最终恢复默认参数再次运行，退出 0。

也可直接打开 `ports/stm32f103-keil/environment-probe/probe.uvprojx` 查看工程。注意配套 `.uvoptx` 必须一同保留，它实际决定软件仿真选项；进入调试会自动执行 `.ini` 并退出。需要交互观察时，另存自己的初始化脚本并移除自动运行／退出命令；这一交互变式尚未独立验证，不将它计入本轮通过结论。

## 3. 文件与代码说明

| 文件 | 用途与设计理由 |
|---|---|
| `ports/stm32f103-keil/environment-probe/probe.c` | 只访问核心寄存器与 RAM。显式初始化全部可观察变量；第 3 次 tick 关闭 SysTick，每次置起 PendSV，避免日志观察时序改变最终计数。`probe_main` 避免 AC5 对 `main` 的运行时入口处理，探针不依赖 C 库初始化 |
| 同目录 `startup.s` | 自写最小向量表与复位入口；MSP 位于 20 KiB RAM 顶端。`read_ipsr` 用 MRS 读取当前异常号；默认异常停留循环，让运行器报错或超时。没有加入上下文切换逻辑 |
| 同目录 `probe.sct` | Flash 64 KiB，RAM 20 KiB；向量表放在加载区首部，RAM 顶端留 1 KiB 给 MSP，不采用非标 Flash 容量假设 |
| 同目录 `probe.uvprojx` | 显式选 AC5、Cortex-M3、C99、调试配置和 scatter；仅编译上述 C/汇编。优化选项 `Optim=1`（本机生成编译参数为 `-O0`，以实际构建响应文件为准） |
| 同目录 `probe.uvoptx` | 选择软件仿真、DARMSTM 参数 `-pSTM32F103C8`、8 MHz 仿真时钟和初始化脚本，禁止自动运行到 main；保留 Keil 读取需要的驱动配置结构，未启用硬件调试 |
| 同目录 `simulator.ini` | 记录辅助启动前后寄存器，运行到完成符号，读取计数、异常号、RAM 与寄存器后写入结束标记并退出。短暂 `_sleep_` 仅让异步调试命令完成，成功由最终状态判定 |
| 同目录 `.gitignore` | 排除 Keil 自动生成的会话与缓存，保留配套工程配置；全局规则已排除 `build/` |
| `tests/scripts/run_keil_probe.ps1` | 独立进程、超时、清除旧产物、构建与日志判定；`ExpectedTicks` 仅用于检查运行器负例 |

工程配置结构参考本机 `C:/Keil_v5/ARM/PACK/Keil/STM32F1xx_DFP/2.2.0/Boards/Keil/MCBSTM32C/Blinky/Blinky.uvprojx` 和配套 `.uvoptx`，仅用于识别配置字段；未复制其 C/汇编实现。已删除原工程源文件分组、RTX/RTE 组件依赖并替换书签为本探针。Keil 自动写回的空 RTE 元数据不构成源代码依赖。

历史教学仓库只查看了目录并取得本地 HEAD `25a06bdea89a467ec7e85fe7f766705b58837961`，该目录未找到 Keil 工程，本轮未复用其实现。用户现有 IDE 工程路径不作为课程正式源码或本轮证据。

## 4. 实际结果与证据

源码环境沿用 `dev`、HEAD `606a23a676b8f7870d1e444e7e48ad3bb9e43fd8` 加当前工作区，未提交或推送。工具见《工作与验证记录》。

最终构建统计：Code=264，RO-data=64，RW-data=28，ZI-data=16；0 Error(s)、0 Warning(s)。

最终观测关键行：

```text
ENV-KEIL-01 started=AC500001 done=600D0001 ticks=3 pendsv=3 thread=0 tick_irq=15 pendsv_irq=14
MEM 12345678 A5A5A5A5 00000528 EDCBA987
REG pc=0800004C sp=20005000 xpsr=61000000
END-KEIL-01
```

PC 随构建改变；稳定判据是它位于探针代码区、SP 在 RAM 范围且按 8 字节对齐、线程和异常号与预期一致、内存值完整匹配。

最终运行日志在探针 `build/build.log`、`build/simulator.log`、`build/session.log`；日期快照在 `tests/build/environment-20261108/keil/`：`build.log`、`simulator.log`、`negative.log`、`positive.log`。这些是被忽略的本机产物，关键结果已保存于本文。下次运行会覆盖探针日志；不得把旧日志冒充新版本结果。

## 5. 实际排错

1. 参考配置 `mOS=1` 会要求 Legacy RTOS Pack。改为 0 后不再依赖该选项，没有安装额外 Pack。
2. 只配置 `.uvprojx` 时，首次调试实际选择 ULINK 并报找不到设备。配套 `.uvoptx` 显式选择 Simulator 后才进入软件仿真。失败有超时保护，不计成功。
3. 清空 Books／驱动注册容器、修改空节点序列后，曾出现选项未被采用并回退硬件驱动的现象；具体哪个解析字段导致回退未断言。最终保留原结构，仅更新目标、仿真字段与书签文本，完整正／负验证通过。工程使用 UTF-8 无 BOM；不能把所有遇到的退出 15 都断言为编码问题。
4. 仿真器 RESET 后 PC/SP 为 0；直接运行发生访问错误。脚本从真实 Flash 向量建立 SP/PC 后，还须设置 Thumb 状态；遗漏 T 位时异常路径仍失败。最终辅助启动后观测符合预期。
5. 原脚本 `D g_memory, g_memory+3` 在本机出现 operand type 错误。改为逐个读取数组元素并核对全部四值，保留同样的 RAM 观察目标。
6. 中途清理配置后的负例曾因调试超时而退出 1，未计为负例验收。最终负例明确到达期望值判定并因 3/4 不符失败，随后正例成功。

## 6. 参考依据

- [μVision 命令行](https://www.keil.com/support/man/docs/uv4/uv4_commandline.asp)：独立构建、`-d` 调试及初始化脚本。核对日期 2026-11-08。
- [Go 命令](https://www.keil.com/support/man/docs/uv4cl/uv4cl_cm_go.htm)：运行到临时断点的语法。
- [预定义调试函数](https://www.keil.com/support/man/docs/uv4cl/uv4cl_df_predeffunct.htm)：`_RDWORD`、`printf`、`_sleep_`。
- [调试输出日志](https://www.keil.com/support/man/docs/uv4cl/uv4cl_db_wrdbg_output.htm)：`LOG` 的记录／关闭行为。
- 已纳入仓库的 `third_party/cmsis/core_cm3.h`（CMSIS 6.1.0）：SCB、SysTick 寄存器布局与相关字段；本探针使用的裸地址与定义核对。此处的功能验证不替代后续架构教材对 ARM 手册章节的复核。


## 7. 无缓存复现补充

本轮仅将 probe.c、startup.s、probe.sct、probe.uvprojx、probe.uvoptx、simulator.ini 六个文件复制到 tests/build/environment-20261108/keil-source-only/，不复制 build、RTE、DebugConfig 或 RAM，再用同样 UV4 -r 与 -d 参数运行。两进程均退出 0，状态、四个 RAM 值和结束标记符合预期；新目录 build 中保留原始日志。证明交付不依赖旧生成缓存，仍依赖本机已安装的 Keil 与设备包，不等于干净读者电脑验收。

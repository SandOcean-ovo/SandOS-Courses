# 验证记录

只记录实际运行过的结果。每条写明源码版本、工具、命令、预期与实际结果。

## V-001 QEMU 双任务冒烟与运行脚本负例（2026-10-31）

| 项 | 值 |
|---|---|
| 源码 | `dev` 分支，基于上游 `25a06bd`，包含本次提交的移植层改动（内核 `os_core.c` 未改） |
| 工具 | arm-none-eabi-gcc 13.3.1（STM32CubeCLT 1.18.0）、QEMU 11.1.3（`mps2-an385`）、mingw32-make 4.4.1、Python 3.14.6 |
| 命令 | `bash tests/scripts/run_all.sh` |
| 结果 | 5 项全部符合预期，`failures: 0` |

| 用例 | 期望 | 实际 |
|---|---|---|
| smoke-two-tasks（`ports/qemu-mps2/main.c`） | 输出 `A0`、`B0`、`A2` 且出现 `TEST:PASS` | 通过 |
| neg-hang | 无输出，脚本超时，退出码 2 | 退出码 2 |
| neg-fail | 输出 `TEST:FAIL`，退出码 1 | 退出码 1 |
| neg-fault | `udf` 触发 HardFault，由故障处理输出 `TEST:FAULT`，退出码 1 | 退出码 1 |
| neg-pass-then-fault | 先 `TEST:PASS` 后 HardFault，必须判失败，退出码 1 | 退出码 1 |

**证明了什么**：GCC 版 PendSV 能完成首次启动和任务切换；SysTick 能唤醒 `OS_Delay` 的任务；运行脚本不会把卡死、无输出、故障当作通过。
**没证明什么**：没有检查栈帧内容；冒烟用例的 A/B 输出顺序在两次运行中不同（`A0 B0 A1 B1` 与 `A0 B0 B1 A1`），因为同优先级任务同一 tick 唤醒的顺序不确定，所以用例只断言偏序。这属于已观察到的现象，是否为缺陷待阶段 2 按契约（FIFO 等待顺序）核对。QEMU 结果不代表真机时序。

### 调试中发现的问题（已修复，供教材参考）
- 链接脚本把 `.init`/`.fini` 当作独立段放在 `.text` 之后时，`_init` 缺少 crtn 提供的返回指令，`__libc_init_array` 调用 `_init` 后跑飞，表现为 QEMU 无任何输出。修复：`.init`、`.fini` 与 `.text` 同一输出段内按输入顺序合并。

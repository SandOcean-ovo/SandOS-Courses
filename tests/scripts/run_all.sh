#!/usr/bin/env bash
##
# @file    run_all.sh
# @brief   构建并运行全部 QEMU 用例：正例必须通过，负例必须得到指定的失败退出码
#
# 用法（在仓库任意位置执行）:
#   bash tests/scripts/run_all.sh
#
# 前置条件: arm-none-eabi-gcc、qemu-system-arm、python、make 都在 PATH 中。
#           Windows 下使用 mingw32-make，其他平台使用 make，脚本自动选择。
#
# 退出码: 0 = 所有用例符合预期；1 = 至少一个用例不符合预期。
#
# 为什么要有负例: 如果运行脚本把“卡死/故障/假通过”误判为通过，
#                 那么所有正例结果都不可信。负例就是在检查检查工具本身。

set -u
cd "$(dirname "$0")/../.."          # 切到仓库根目录，后面的路径都以它为基准

MAKE=$(command -v mingw32-make || command -v make)
PYTHON=${PYTHON:-python}
RUN="$PYTHON tests/scripts/run_qemu.py"
fails=0                              # 不符合预期的用例数
LOG_DIR=tests/build/logs
mkdir -p "$LOG_DIR"

##
# @brief  构建一个应用/测试程序，保留构建日志，失败时打印
# @param  $1 APP 源文件路径，相对于 ports/qemu-mps2
# @return 0 构建成功；1 构建失败
build() {
    "$MAKE" -C ports/qemu-mps2 APP="$1" >"$LOG_DIR/$2.build.log" 2>&1 \
        || { cat "$LOG_DIR/$2.build.log"; echo "BUILD FAIL: $1"; return 1; }
}

##
# @brief  正例: 构建、运行，要求判定为通过且期望文本按序出现
# @param  $1 用例名
# @param  $2 源文件路径，相对于 ports/qemu-mps2
# @param  $@ 其余参数: 期望按顺序出现的输出文本
pos() {
    local name=$1 src=$2; shift 2
    local args=(--case "$name")
    for e in "$@"; do args+=(--expect "$e"); done
    if build "$src" "$name" && $RUN "ports/qemu-mps2/build/$(basename "${src%.c}").elf" \
            "${args[@]}" >"$LOG_DIR/$name.log" 2>&1; then
        echo "PASS  $name"
    else
        cat "$LOG_DIR/$name.log" 2>/dev/null
        echo "FAIL  $name"; fails=$((fails+1))
    fi
}

##
# @brief  负例: 构建、运行，要求运行脚本给出指定的退出码
# @param  $1 用例名
# @param  $2 源文件路径，相对于 ports/qemu-mps2
# @param  $3 期望的运行脚本退出码（1=失败，2=超时）
neg() {
    local name=$1 src=$2 want=$3
    build "$src" "$name" || { fails=$((fails+1)); return; }
    $RUN "ports/qemu-mps2/build/$(basename "${src%.c}").elf" --case "$name" --timeout 3 >"$LOG_DIR/$name.log" 2>&1
    local got=$?
    if [ "$got" = "$want" ]; then
        echo "PASS  $name (runner exit $got as expected)"
    else
        cat "$LOG_DIR/$name.log"
        echo "FAIL  $name (runner exit $got, expected $want)"; fails=$((fails+1))
    fi
}

##
# @brief  GDB 检查: 构建 main.c，用 GDB 脚本检查目标内部状态
# @param  $1 用例名
# @param  $2 GDB 脚本路径（相对仓库根目录）
# @param  $3 期望的 run_gdb.py 退出码（0=通过，1=检查失败，2=超时）
gdbcase() {
    local name=$1 script=$2 want=$3 src=${4:-main.c}
    build "$src" "$name" || { fails=$((fails+1)); return; }
    "$PYTHON" tests/scripts/run_gdb.py "ports/qemu-mps2/build/$(basename "${src%.c}").elf" "$script" --timeout 15 >"$LOG_DIR/$name.log" 2>&1
    local got=$?
    if [ "$got" = "$want" ]; then
        echo "PASS  $name (gdb runner exit $got as expected)"
    else
        cat "$LOG_DIR/$name.log"
        echo "FAIL  $name (gdb runner exit $got, expected $want)"; fails=$((fails+1))
    fi
}

# ---- 用例清单 ----
pos SMOKE-01 main.c A0 B0 A2                                              # 两任务冒烟
pos SCHED-01 ../../tests/target/task_yield.c "EVENT 1" "EVENT 7"
pos SCHED-02 ../../tests/target/tick_preempt.c "EVENT 1" "EVENT 4"
pos SCHED-03 ../../tests/target/delay_wake.c "EVENT 1" "EVENT 3"
pos SEM-01 ../../tests/target/sem_count.c
pos SEM-02 ../../tests/target/sem_wait_fifo.c "EVENT 1" "EVENT 7"
pos MUTEX-01 ../../tests/target/mutex_handoff.c "EVENT 1" "EVENT 10"
pos MUTEX-02 ../../tests/target/mutex_bounds.c
pos CTX-01 ../../tests/target/blocking_context.c
pos CTX-05 ../../tests/target/blocking_context_legal.c
pos CRIT-01 ../../tests/target/critical_restore.c
pos CRIT-02 ../../tests/target/irq_restore.c
neg CRITNEG-01 ../../tests/target/negative/neg_critical_exit.c 1
neg CRITNEG-02 ../../tests/target/negative/neg_critical_isr.c 1
pos ISR-01 ../../tests/target/isr_flags.c
pos ISR-02 ../../tests/target/isr_wakeup.c "EVENT 1" "EVENT 14"
pos ISR-05 ../../tests/target/isr_priority.c
neg ISRNEG-01 ../../tests/target/negative/neg_yield_isr_thread.c 1
neg NEG-01 ../../tests/target/negative/neg_hang.c           2            # 卡死 -> 超时
neg NEG-02 ../../tests/target/negative/neg_fail.c           1            # 主动失败
neg NEG-03 ../../tests/target/negative/neg_fault.c          1            # HardFault
neg NEG-04 ../../tests/target/negative/neg_pass_text_only.c 1            # 先 PASS 后故障
neg NEG-05 ../../tests/target/negative/neg_pass_hang.c      2            # PASS 后卡死
neg NEG-06 ../../tests/target/negative/neg_exit_status.c    1            # PASS + 退出 7
neg NEG-07 ../../tests/target/negative/neg_exit_no_pass.c   1            # 正常退出但无 PASS
neg NEG-08 ../../tests/target/negative/neg_assert.c         1            # 内核断言
neg NEG-09 ../../tests/target/negative/neg_event_order.c    1            # 内存事件顺序出错
neg NEG-10 ../../tests/target/negative/neg_wrong_case.c     1            # 测试编号不匹配

# GDB 检查：栈帧内容。GDBNEG 用来证明 GDB 检查本身不会空通过或永远通过
gdbcase GDB-01    tests/gdb/pendsv_frame.gdb                   0       # 首次切换后的栈帧
# 原生断言断点：核对拒绝发生前任务、队列和屏蔽状态均未改变。
gdbcase CTX-02 tests/gdb/delay_context_assert.gdb 0 ../../tests/target/delay_context_critical.c
gdbcase CTX-03 tests/gdb/delay_context_assert.gdb 0 ../../tests/target/delay_context_isr.c
gdbcase CTX-04 tests/gdb/delay_context_assert.gdb 0 ../../tests/target/delay_context_masked.c
# GDB 精确挂起 IRQ，检查资源与 tick 更新不能被更高优先级 ISR 拆开。
gdbcase ISR-03 tests/gdb/isr_atomic_svc.gdb 0 ../../tests/target/isr_atomic_svc.c
gdbcase ISR-04 tests/gdb/isr_atomic_tick.gdb 0 ../../tests/target/isr_atomic_tick.c
gdbcase GDBNEG-01 tests/gdb/negative/neg_corrupt_frame.gdb     1       # 栈帧被破坏 -> 检查失败
gdbcase GDBNEG-02 tests/gdb/negative/neg_never_hit.gdb         2 ../../tests/target/negative/neg_hang.c
gdbcase GDBNEG-03 tests/gdb/negative/neg_no_check.gdb          1       # 没有任何检查输出 -> 失败
gdbcase GDBNEG-04 tests/gdb/negative/neg_error_after_pass.gdb  1       # PASS 后命令出错 -> 失败
gdbcase GDBNEG-05 tests/gdb/negative/neg_exit_without_teardown.gdb 1    # 提前退出无清理握手 -> 失败

if "$PYTHON" tests/scripts/run_host.py >"$LOG_DIR/HOST.log" 2>&1; then
    echo "PASS  HOST (27 checks and 2 negative cases)"
else
    cat "$LOG_DIR/HOST.log"
    echo "FAIL  HOST"; fails=$((fails+1))
fi

echo "logs: $LOG_DIR"
echo "failures: $fails"
exit $((fails > 0))

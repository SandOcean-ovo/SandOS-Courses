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
RUN="python tests/scripts/run_qemu.py"
fails=0                              # 不符合预期的用例数

##
# @brief  构建一个应用/测试程序（输出被丢弃，失败时打印提示）
# @param  $1 APP 源文件路径，相对于 ports/qemu-mps2
# @return 0 构建成功；1 构建失败
build() {
    "$MAKE" -C ports/qemu-mps2 APP="$1" >/dev/null \
        || { echo "BUILD FAIL: $1"; return 1; }
}

##
# @brief  正例: 构建、运行，要求判定为通过且期望文本按序出现
# @param  $1 用例名
# @param  $2 源文件路径，相对于 ports/qemu-mps2
# @param  $@ 其余参数: 期望按顺序出现的输出文本
pos() {
    local name=$1 src=$2; shift 2
    local args=()
    for e in "$@"; do args+=(--expect "$e"); done
    if build "$src" && $RUN "ports/qemu-mps2/build/$(basename "${src%.c}").elf" \
            "${args[@]}" >/dev/null; then
        echo "PASS  $name"
    else
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
    build "$src" || { fails=$((fails+1)); return; }
    $RUN "ports/qemu-mps2/build/$(basename "${src%.c}").elf" --timeout 3 >/dev/null
    local got=$?
    if [ "$got" = "$want" ]; then
        echo "PASS  $name (runner exit $got as expected)"
    else
        echo "FAIL  $name (runner exit $got, expected $want)"; fails=$((fails+1))
    fi
}

# ---- 用例清单 ----
pos SMOKE-01 main.c A0 B0 A2                                              # 两任务冒烟
neg NEG-01 ../../tests/target/negative/neg_hang.c           2            # 卡死 -> 超时
neg NEG-02 ../../tests/target/negative/neg_fail.c           1            # 主动失败
neg NEG-03 ../../tests/target/negative/neg_fault.c          1            # HardFault
neg NEG-04 ../../tests/target/negative/neg_pass_text_only.c 1            # 先 PASS 后故障

echo "failures: $fails"
exit $((fails > 0))

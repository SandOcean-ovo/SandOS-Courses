#!/usr/bin/env bash
# 构建并运行全部 QEMU 用例。正例必须通过；负例必须得到指定的失败退出码。
# 用法: bash tests/scripts/run_all.sh   (Windows 下使用 mingw32-make，其他平台用 make)
set -u
cd "$(dirname "$0")/../.."
MAKE=$(command -v mingw32-make || command -v make)
RUN="python tests/scripts/run_qemu.py"
fails=0

build() { "$MAKE" -C ports/qemu-mps2 APP="$1" >/dev/null || { echo "BUILD FAIL: $1"; return 1; }; }

# 正例: 名称 源文件 期望文本...
pos() {
    local name=$1 src=$2; shift 2
    local args=(); for e in "$@"; do args+=(--expect "$e"); done
    if build "$src" && $RUN "ports/qemu-mps2/build/$(basename "${src%.c}").elf" "${args[@]}" >/dev/null; then
        echo "PASS  $name"
    else echo "FAIL  $name"; fails=$((fails+1)); fi
}

# 负例: 名称 源文件 期望退出码
neg() {
    local name=$1 src=$2 want=$3
    build "$src" || { fails=$((fails+1)); return; }
    $RUN "ports/qemu-mps2/build/$(basename "${src%.c}").elf" --timeout 3 >/dev/null; local got=$?
    if [ "$got" = "$want" ]; then echo "PASS  $name (runner exit $got as expected)"
    else echo "FAIL  $name (runner exit $got, expected $want)"; fails=$((fails+1)); fi
}

pos smoke-two-tasks main.c A0 B0 A2
neg neg-hang        ../../tests/target/negative/neg_hang.c 2
neg neg-fail        ../../tests/target/negative/neg_fail.c 1
neg neg-fault       ../../tests/target/negative/neg_fault.c 1
neg neg-pass-then-fault ../../tests/target/negative/neg_pass_text_only.c 1

echo "failures: $fails"
exit $((fails > 0))

#!/usr/bin/env python3
"""
@file    run_gdb.py
@brief   启动 QEMU（暂停状态）并用 GDB 脚本检查目标内部状态

用法:
    python tests/scripts/run_gdb.py <elf> <gdb脚本> [--timeout 秒] [--port 端口]

流程:
    1. 启动 qemu-system-arm -S -gdb tcp::<port>（CPU 暂停，等待 GDB 连接）；
    2. 启动 arm-none-eabi-gdb -nx -batch，连接本地端口后加载脚本；
    3. 解析 GDB 输出里的 "GDBCHK <名称> PASS|FAIL" 与 "GDBRESULT PASS|FAIL"；
    4. 无论结果如何，都结束 QEMU 与 GDB，不留残留进程。

判定（全部满足才算通过，退出码 0）:
    - 出现 "GDBRESULT PASS"；
    - 没有任何 "GDBCHK ... FAIL"；
    - 至少出现一条 "GDBCHK"（防止脚本没跑到检查点就“空通过”）。
    - GDB 正常退出，QEMU 在清理前仍存活，且没有 GDBRESULT FAIL。

退出码:
    0  通过
    1  检查失败，或 GDB 输出里没有任何检查结果
    2  超时（GDB 一直没返回，例如断点没有被命中）
    3  QEMU 或 GDB 无法启动

超时、无检查输出都不会被当作通过。
"""
import argparse
import os
import re
import socket
import subprocess
import sys

## QEMU 与 GDB 的可执行文件名，需在 PATH 中
QEMU = "qemu-system-arm"
GDB = os.environ.get("GDB", "arm-none-eabi-gdb")


def main():
    """
    @brief  脚本入口：协调 QEMU 与 GDB，汇总检查结果
    @return 进程退出码，含义见文件头部说明
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", help="带调试信息的 ELF 文件")
    ap.add_argument("script", help="GDB 命令脚本")
    ap.add_argument("--timeout", type=float, default=30.0,
                    help="GDB 最长运行秒数（默认 30）")
    ap.add_argument("--port", type=int, default=0,
                    help="本地 GDB 端口（默认选择空闲端口；脚本无需自行连接）")
    a = ap.parse_args()
    if a.port == 0:
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            a.port = listener.getsockname()[1]

    qemu_cmd = [QEMU, "-M", "mps2-an385", "-cpu", "cortex-m3", "-nographic",
                "-monitor", "none", "-serial", "null",
                "-semihosting-config", "enable=on,target=native",
                "-S", "-gdb", f"tcp:127.0.0.1:{a.port}", "-kernel", a.elf]
    gdb_cmd = [GDB, "-nx", "-batch", "-ex", "set pagination off",
               "-ex", "set confirm off", "-ex", "set tcp connect-timeout 5",
               "-ex", f"target remote 127.0.0.1:{a.port}", "-x", a.script, a.elf]

    try:
        qemu = subprocess.Popen(qemu_cmd, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
    except OSError as e:
        print(f"RESULT: ERROR cannot start qemu: {e}")
        return 3

    out = ""
    timed_out = False
    try:
        # communicate 带超时：超时后必须 kill，再取走剩余输出，否则可能残留进程
        gdb = subprocess.Popen(gdb_cmd, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
        try:
            raw, _ = gdb.communicate(timeout=a.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            gdb.kill()
            raw, _ = gdb.communicate()
        out = raw.decode("utf-8", "replace")
    except OSError as e:
        qemu.kill()
        qemu.wait()
        print(f"RESULT: ERROR cannot start gdb: {e}")
        return 3
    finally:
        qemu_exit = qemu.poll()
        qemu.kill()
        qemu.wait()

    # 只打印检查相关的行，避免 GDB 的启动噪声淹没结果
    lines = [l for l in out.splitlines() if l.startswith("GDB")]
    print("\n".join(lines))

    if timed_out:
        print(f"RESULT: TIMEOUT after {a.timeout}s (gdb did not finish)")
        return 2

    checks = [l for l in lines if re.fullmatch(r"GDBCHK \S+ (?:PASS|FAIL)(?: .*)?", l)]
    failed = [l for l in checks if " FAIL" in l]
    if gdb.returncode != 0 or qemu_exit is not None:
        print(out)
        print(f"RESULT: FAIL process exit gdb={gdb.returncode} qemu={qemu_exit}")
        return 1
    if not checks:
        print(out)
        print("RESULT: FAIL no GDBCHK lines (breakpoint not reached or script error)")
        return 1
    if failed or "GDBRESULT PASS" not in lines or any(l.startswith("GDBRESULT FAIL") for l in lines):
        print(f"RESULT: FAIL {len(failed)} check(s) failed")
        return 1
    print(f"RESULT: PASS ({len(checks)} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

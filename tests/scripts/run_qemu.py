#!/usr/bin/env python3
"""运行 mps2-an385 ELF：完整 PASS 行、预期输出和 QEMU 正常退出三者缺一不可。

退出码：0 通过；1 标记/输出/退出状态不符；2 超时；3 无法启动 QEMU。
PASS 后不退出也会超时；不再使用固定观察期推定完成。
"""
import argparse
from pathlib import Path
import subprocess
import sys
import threading
import time

QEMU = "qemu-system-arm"
BAD = ("TEST:FAIL", "TEST:FAULT", "OS ASSERT FAILED")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--case", help="要求出现完整 CASE 编号行")
    ap.add_argument("--log", help="另存 QEMU 原始输出")
    a = ap.parse_args()
    cmd = [QEMU, "-M", "mps2-an385", "-cpu", "cortex-m3", "-nographic",
           "-monitor", "none", "-serial", "stdio",
           "-semihosting-config", "enable=on,target=native", "-kernel", a.elf]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as error:
        print(f"RESULT: ERROR cannot start qemu: {error}")
        return 3

    buf = bytearray()
    lock = threading.Lock()

    def reader():
        for chunk in iter(lambda: proc.stdout.read1(4096), b""):
            with lock:
                buf.extend(chunk)

    def output():
        with lock:
            return buf.decode("utf-8", "replace")

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    deadline = time.monotonic() + a.timeout
    timed_out = False
    bad = None
    try:
        while proc.poll() is None:
            bad = next((marker for marker in BAD if marker in output()), None)
            if bad:
                break
            if time.monotonic() >= deadline:
                timed_out = True
                break
            time.sleep(0.01)
    finally:
        # 只有目标自然退出才可能通过；脚本主动 kill 绝不能产生通过结果。
        native_exit = proc.poll()
        if native_exit is None:
            proc.kill()
        proc.wait()
        thread.join()
        proc.stdout.close()

    out = output()
    if a.log:
        Path(a.log).write_text(out, encoding="utf-8")
    print(out, end="" if out.endswith("\n") or not out else "\n")
    bad = next((marker for marker in BAD if marker in out), bad)
    if bad:
        print(f"RESULT: FAIL saw {bad} (qemu exit={native_exit})")
        return 1
    if timed_out:
        print(f"RESULT: TIMEOUT after {a.timeout}s; target did not exit (output bytes={len(out)})")
        return 2
    if native_exit != 0:
        print(f"RESULT: FAIL qemu exit={native_exit}, expected 0")
        return 1
    if "TEST:PASS" not in out.splitlines():
        print("RESULT: FAIL missing complete TEST:PASS line")
        return 1
    if a.case and f"CASE {a.case}" not in out.splitlines():
        print(f"RESULT: FAIL missing CASE {a.case}")
        return 1
    position = 0
    for expected in a.expect:
        found = out.find(expected, position)
        if found < 0:
            print(f"RESULT: FAIL missing/out-of-order expected text: {expected!r}")
            return 1
        position = found + len(expected)
    print("RESULT: PASS qemu exit=0")
    return 0


if __name__ == "__main__":
    sys.exit(main())

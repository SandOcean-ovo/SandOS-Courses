#!/usr/bin/env python3
"""运行 QEMU mps2-an385 并按串口输出判定结果。

用法: run_qemu.py <elf> [--timeout 秒] [--expect 文本 ...] [--log 路径]

通过条件（全部满足才返回 0）:
  1. 串口输出出现 TEST:PASS；
  2. 所有 --expect 文本按顺序出现；
  3. 未出现 TEST:FAIL、TEST:FAULT、OS ASSERT FAILED。
否则返回 1；超时返回 2；QEMU 无法启动返回 3。
超时、没有输出、异常退出都不会被当作通过。
"""
import argparse, subprocess, sys, threading, time

QEMU = "qemu-system-arm"
GRACE = 0.5   # 看到 TEST:PASS 后继续观察的秒数
BAD = ("TEST:FAIL", "TEST:FAULT", "OS ASSERT FAILED")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--log")
    a = ap.parse_args()

    cmd = [QEMU, "-M", "mps2-an385", "-cpu", "cortex-m3", "-nographic",
           "-monitor", "none", "-serial", "stdio", "-kernel", a.elf]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        print(f"RESULT: ERROR cannot start qemu: {e}")
        return 3

    buf = bytearray()
    lock = threading.Lock()

    def reader():
        for chunk in iter(lambda: proc.stdout.read1(4096), b""):
            with lock:
                buf.extend(chunk)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def text():
        with lock:
            return buf.decode("utf-8", "replace")

    deadline = time.monotonic() + a.timeout
    verdict = None
    while time.monotonic() < deadline:
        out = text()
        bad = next((b for b in BAD if b in out), None)
        if bad:
            verdict = ("FAIL", f"saw {bad}")
            break
        if "TEST:PASS" in out:
            # 宽限期：PASS 之后紧跟的故障/断言也要能被发现
            time.sleep(GRACE)
            out = text()
            bad = next((b for b in BAD if b in out), None)
            verdict = ("FAIL", f"saw {bad} after TEST:PASS") if bad else ("PASS", "")
            break
        if proc.poll() is not None:
            verdict = ("FAIL", f"qemu exited early (code {proc.returncode}) without TEST:PASS")
            break
        time.sleep(0.05)

    proc.kill()
    proc.wait()
    t.join(timeout=1)
    out = text()
    if a.log:
        with open(a.log, "w", encoding="utf-8") as f:
            f.write(out)
    sys.stdout.write(out if out.endswith("\n") or not out else out + "\n")

    if verdict is None:
        print(f"RESULT: TIMEOUT after {a.timeout}s (output bytes: {len(out)})")
        return 2
    if verdict[0] == "PASS":
        pos = 0
        for e in a.expect:
            i = out.find(e, pos)
            if i < 0:
                print(f"RESULT: FAIL missing/out-of-order expected text: {e!r}")
                return 1
            pos = i + len(e)
        print("RESULT: PASS")
        return 0
    print(f"RESULT: {verdict[0]} {verdict[1]}")
    return 1


if __name__ == "__main__":
    sys.exit(main())

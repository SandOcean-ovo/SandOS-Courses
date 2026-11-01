#!/usr/bin/env python3
"""
@file    run_qemu.py
@brief   在 QEMU mps2-an385 上运行一个 ELF，按串口输出判定测试结果

用法:
    python tests/scripts/run_qemu.py <elf> [--timeout 秒] [--expect 文本 ...] [--log 路径]

判定规则（全部满足才算通过，退出码 0）:
    1. 串口输出出现 "TEST:PASS"；
    2. 所有 --expect 给出的文本按顺序出现（只断言偏序，不要求相邻）；
    3. 没有出现 "TEST:FAIL"、"TEST:FAULT"、"OS ASSERT FAILED"，
       包括 "TEST:PASS" 之后的一小段观察期（GRACE）内。

退出码约定（run_all.sh 的负例依赖这些数值）:
    0  通过
    1  失败（出现失败/故障标记、QEMU 提前退出、缺少期望文本）
    2  超时（没有在限定时间内出现 TEST:PASS，包括完全没有输出）
    3  QEMU 无法启动（例如 qemu-system-arm 不在 PATH 中）

关键原则：超时、无输出、异常退出都不会被当作通过。
"""
import argparse
import subprocess
import sys
import threading
import time

## QEMU 可执行文件名，需在 PATH 中
QEMU = "qemu-system-arm"

## 看到 TEST:PASS 后继续观察的秒数。
# 用来发现“先打印 PASS、随后才崩溃”这类假通过。
GRACE = 0.5

## 任何一个出现就判失败的标记
BAD = ("TEST:FAIL", "TEST:FAULT", "OS ASSERT FAILED")


def main():
    """
    @brief  脚本入口：启动 QEMU、收集串口输出、给出判定
    @return 进程退出码，含义见文件头部说明
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", help="要运行的 ELF 文件")
    ap.add_argument("--timeout", type=float, default=10.0,
                    help="最长等待秒数（默认 10）")
    ap.add_argument("--expect", action="append", default=[],
                    help="必须按顺序出现的输出文本，可重复给出")
    ap.add_argument("--log", help="把完整串口输出另存到该文件")
    a = ap.parse_args()

    # -serial stdio：把 UART0 接到本进程的管道；-monitor none 关闭 QEMU 控制台
    cmd = [QEMU, "-M", "mps2-an385", "-cpu", "cortex-m3", "-nographic",
           "-monitor", "none", "-serial", "stdio", "-kernel", a.elf]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
    except OSError as e:
        print(f"RESULT: ERROR cannot start qemu: {e}")
        return 3

    # 用后台线程持续读取输出，避免主线程阻塞在 read 上导致无法超时
    buf = bytearray()
    lock = threading.Lock()

    def reader():
        """@brief 读线程：把 QEMU 的输出不断追加到 buf"""
        for chunk in iter(lambda: proc.stdout.read1(4096), b""):
            with lock:
                buf.extend(chunk)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def text():
        """@brief 取当前已收到的全部输出（解码失败的字节用替换字符）"""
        with lock:
            return buf.decode("utf-8", "replace")

    # 轮询：先查失败标记，再查通过标记，最后查 QEMU 是否提前退出
    deadline = time.monotonic() + a.timeout
    verdict = None          # None 表示到时间仍没有结论
    while time.monotonic() < deadline:
        out = text()
        bad = next((b for b in BAD if b in out), None)
        if bad:
            verdict = ("FAIL", f"saw {bad}")
            break
        if "TEST:PASS" in out:
            # 观察期：PASS 之后立刻出现的故障/断言也要能发现
            time.sleep(GRACE)
            out = text()
            bad = next((b for b in BAD if b in out), None)
            verdict = (("FAIL", f"saw {bad} after TEST:PASS") if bad
                       else ("PASS", ""))
            break
        if proc.poll() is not None:
            verdict = ("FAIL", f"qemu exited early (code {proc.returncode}) "
                               "without TEST:PASS")
            break
        time.sleep(0.05)

    # 无论结果如何，都结束 QEMU 并取完输出
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
        # 期望文本必须按给定顺序出现；pos 记录上一个匹配的结束位置
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

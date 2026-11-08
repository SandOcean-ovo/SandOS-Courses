#!/usr/bin/env python3
"""构建并运行纯 C 主机测试；检查结果标记、退出码和超时，完整日志交给调用者保存。"""
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    make = shutil.which("mingw32-make") or shutil.which("make")
    if not make:
        print("RESULT: ERROR make not found")
        return 1
    try:
        build = subprocess.run([make, "-C", "tests/host", "all-build"],
                               cwd=ROOT, timeout=60)
        if build.returncode:
            return 1
        binary = ROOT / "tests/host/build/test_host.exe"
        cases = [("HOST", [], 0, r"HOSTSUMMARY total=[1-9][0-9]* failed=0", "HOSTRESULT PASS"),
                 ("HOSTNEG-01", ["--negative-check"], 1,
                  r"CASE HOSTNEG-01 FAIL .*", "HOSTRESULT FAIL"),
                 ("HOSTNEG-02", ["--negative-assert"], 1,
                  r"CASE UNEXPECTED_ASSERT FAIL .*", "HOSTRESULT FAIL")]
        for name, args, want, pattern, marker in cases:
            run = subprocess.run([str(binary), *args], cwd=ROOT, timeout=10,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            out = run.stdout.decode("utf-8", "replace")
            print(out, end="")
            if (run.returncode != want or marker not in out.splitlines()
                    or not re.search(pattern, out)):
                print(f"RESULT: FAIL {name} exit={run.returncode}, expected={want}")
                return 1
            if want == 0 and re.search(r"^CASE .* FAIL(?: |$)", out, re.M):
                print("RESULT: FAIL host check failed before PASS")
                return 1
            print(f"PASS  {name}")
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"RESULT: FAIL {error}")
        return 1
    print("RESULT: PASS host suite")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# ISR-03：在 sem 等待者摘链前注入 IRQ，检查整次交接已经被屏蔽保护。
set pagination off
set confirm off
set $expected_ipsr = 11
tbreak OS_TaskResume
continue
source tests/gdb/isr_atomic_checks.gdb

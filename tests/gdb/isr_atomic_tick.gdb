# ISR-04：生产 tick 更新延时链表时注入 IRQ，验证与 FromISR 共用同一屏蔽规则。
set pagination off
set confirm off
set $expected_ipsr = 15
tbreak OS_DelayListAdvance
continue
source tests/gdb/isr_atomic_checks.gdb

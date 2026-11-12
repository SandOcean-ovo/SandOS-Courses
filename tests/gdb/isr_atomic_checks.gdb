# ISR-03/04：由 wrapper 设置断点；必须在真实 Handler 且 PRIMASK=1 的更新窗口。
set $fails = 0
if $primask == 1 && ($xpsr & 0x1ff) == $expected_ipsr && g_CriticalNesting == 0
  printf "GDBCHK shared_update_is_masked_without_task_nesting PASS\n"
else
  printf "GDBCHK shared_update_is_masked_without_task_nesting FAIL\n"
  set $fails = $fails + 1
end
# 用 fixture helper 让目标 CPU 执行 NVIC 写操作；避免旧 QEMU 忽略 GDB 直接写 PPB。
# 此时 PRIMASK 已由生产代码置 1，dummy call 不应使 IRQ 越过保护区执行。
call isr_test_pend_irq()
printf "GDBDIAG INJECT primask=%u pending=%#x enabled=%#x irq_seen=%u\n", $primask, *(unsigned int *)0xe000e200, *(unsigned int *)0xe000e100, g_irq_seen
if g_irq_seen == 0 && $primask == 1 && (*(unsigned int *)0xe000e200 & 1) != 0
  printf "GDBCHK injected_irq_not_run_inside_guard PASS\n"
else
  printf "GDBCHK injected_irq_not_run_inside_guard FAIL\n"
  set $fails = $fails + 1
end
tbreak test_finish
continue
printf "GDBDIAG OBS finished=%#x irq_seen=%u observed=%u ipsr=%u high_ran=%u tick=%u mask=%u state=%u\n", g_finished, g_irq_seen, g_observed_ok, g_irq_ipsr, g_high_ran, g_SystemTickCount, $primask, high.State
if g_finished == 0x150cafe && g_irq_seen == 1 && g_observed_ok == 1 && g_irq_ipsr == 16
  printf "GDBCHK irq_observes_completed_outer_update PASS\n"
else
  printf "GDBCHK irq_observes_completed_outer_update FAIL\n"
  set $fails = $fails + 1
end
if g_high_ran == 1 && CurrentTCB == &low && high.State == TASK_BLOCKED && sem.WaitList.Head == 0 && g_CriticalNesting == 0 && $primask == 0
  printf "GDBCHK high_runs_before_low_resumes_with_consistent_state PASS\n"
else
  printf "GDBCHK high_runs_before_low_resumes_with_consistent_state FAIL\n"
  set $fails = $fails + 1
end
if $fails == 0
  printf "GDBRESULT PASS\n"
else
  printf "GDBRESULT FAIL\n"
end
kill
printf "GDBTEARDOWN DONE\n"
quit

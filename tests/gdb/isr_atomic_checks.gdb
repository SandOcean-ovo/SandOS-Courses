# ISR-03/04：由 wrapper 设置断点；必须在真实 Handler 且 PRIMASK=1 的更新窗口。
set $fails = 0
if $primask == 1 && ($xpsr & 0x1ff) == $expected_ipsr && g_CriticalNesting == 0
  printf "GDBCHK shared_update_is_masked_without_task_nesting PASS\n"
else
  printf "GDBCHK shared_update_is_masked_without_task_nesting FAIL\n"
  set $fails = $fails + 1
end
# NVIC ISPR0 是置位寄存器；写 bit0 挂起真实 IRQ0。保护正确时它暂时不能执行。
set *(unsigned int *)0xe000e200 = 1
if g_irq_seen == 0
  printf "GDBCHK injected_irq_not_run_inside_guard PASS\n"
else
  printf "GDBCHK injected_irq_not_run_inside_guard FAIL\n"
  set $fails = $fails + 1
end
tbreak test_finish
continue
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

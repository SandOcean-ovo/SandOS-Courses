# START-02：复用 START-01 夹具，在 PSP 已有效但状态尚未发布的窗口再挂起一个 tick。
set pagination off
set confirm off
set $fails = 0
tbreak PendSV_StackReady
continue
if g_OSRunning == 2 && CurrentTCB == &runner && $primask == 1 && ($xpsr & 0x1ff) == 14 && $psp == (unsigned int)(runner.stackPtr + 8)
  printf "GDBCHK stack_ready_precedes_running_publication PASS\n"
else
  printf "GDBCHK stack_ready_precedes_running_publication FAIL\n"
  set $fails = $fails + 1
end
set g_expect_late_tick = 1
# 让 CPU 自己写 SCB，避免旧 QEMU 忽略调试器直接写 PPB 寄存器。
call startup_test_pend_tick()
if $primask == 1 && g_OSRunning == 2 && g_live_tick_seen == 0 && (*(unsigned int *)0xe000ed04 & 0x04000000) != 0
  printf "GDBCHK late_tick_pending_until_commit PASS\n"
else
  printf "GDBCHK late_tick_pending_until_commit FAIL\n"
  set $fails = $fails + 1
end
tbreak test_finish
continue
if g_finished == 0x57a47001 && g_stopped_tick_seen == 1 && g_early_tick_seen == 1 && g_early_tick_intact == 1 && g_early_state == 2 && g_early_ipsr == 15 && g_live_tick_seen == 2 && g_SystemTickCount == 2
  printf "GDBCHK startup_tick_ignored_and_committed_ticks_counted PASS\n"
else
  printf "GDBCHK startup_tick_ignored_and_committed_ticks_counted FAIL\n"
  set $fails = $fails + 1
end
if g_OSRunning == 1 && CurrentTCB == &runner && g_peer_ran == 1 && peer.State == TASK_BLOCKED && g_CriticalNesting == 0 && $primask == 0
  printf "GDBCHK normal_switch_preserves_running_state PASS\n"
else
  printf "GDBCHK normal_switch_preserves_running_state FAIL\n"
  set $fails = $fails + 1
end
if 'test_support.c'::failures == 0
  printf "GDBCHK all_fixture_checks_passed PASS\n"
else
  printf "GDBCHK all_fixture_checks_passed FAIL\n"
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

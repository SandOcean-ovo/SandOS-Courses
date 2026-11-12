# TICK-02：真实启动失败，断在原生断言入口核对回滚与硬件状态。
set pagination off
set confirm off
set $fails = 0
tbreak OS_AssertFailed
continue
if g_attempt == 0x71cfa11 && g_clock_reads == 1 && g_OSRunning == 0 && CurrentTCB == 0 && NextTCB == 0
  printf "GDBCHK invalid_clock_start_restores_stopped PASS\n"
else
  printf "GDBCHK invalid_clock_start_restores_stopped FAIL\n"
  set $fails = $fails + 1
end
set $i = 0
set $bad = 0
while $i < sizeof(g_ready_before)
  if ((unsigned char *)ReadyList)[$i] != ((unsigned char *)g_ready_before)[$i]
    set $bad = 1
  end
  set $i = $i + 1
end
if $bad == 0 && g_PrioMap == g_map_before && g_SystemTickCount == 0 && DelayList.Head == g_delay_before.Head && DelayList.Tail == g_delay_before.Tail
  printf "GDBCHK invalid_clock_keeps_scheduler_queues PASS\n"
else
  printf "GDBCHK invalid_clock_keeps_scheduler_queues FAIL\n"
  set $fails = $fails + 1
end
if *(unsigned int *)0xe000e014 == 77 && (*(unsigned int *)0xe000e010 & 7) == g_control_before && *(unsigned int *)0xe000e018 == 0
  printf "GDBCHK invalid_clock_keeps_systick_registers PASS\n"
else
  printf "GDBCHK invalid_clock_keeps_systick_registers FAIL\n"
  set $fails = $fails + 1
end
if *(unsigned char *)0xe000ed22 == g_pend_priority && *(unsigned char *)0xe000ed23 == g_tick_priority
  printf "GDBCHK invalid_clock_keeps_exception_priorities PASS\n"
else
  printf "GDBCHK invalid_clock_keeps_exception_priorities FAIL\n"
  set $fails = $fails + 1
end
if $primask == 0 && g_CriticalNesting == 0 && (*(unsigned int *)0xe000ed04 & 0x14000000) == 0
  printf "GDBCHK invalid_clock_restores_mask_without_pendsv PASS\n"
else
  printf "GDBCHK invalid_clock_restores_mask_without_pendsv FAIL\n"
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

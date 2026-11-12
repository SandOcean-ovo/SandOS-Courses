# CTX-02/03/04：断在真实生产断言入口；禁止通过调用目标函数读取状态。
set pagination off
set confirm off
tbreak OS_AssertFailed
continue
set $fails = 0
if g_attempted == 0xc07e0001 && g_returned == 0
  printf "GDBCHK delay_guard_asserts_before_return PASS\n"
else
  printf "GDBCHK delay_guard_asserts_before_return FAIL\n"
  set $fails = $fails + 1
end

# 按字节比较 TCB 和全部就绪表，包含链表头尾与持锁字段，避免只检查一个 State。
set $i = 0
set $bad = 0
while $i < sizeof(runner)
  if ((unsigned char *)&runner)[$i] != ((unsigned char *)&g_task_before)[$i]
    set $bad = 1
  end
  set $i = $i + 1
end
if $bad == 0
  printf "GDBCHK delay_tcb_unchanged PASS\n"
else
  printf "GDBCHK delay_tcb_unchanged FAIL\n"
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
if $bad == 0
  printf "GDBCHK delay_all_ready_queues_unchanged PASS\n"
else
  printf "GDBCHK delay_all_ready_queues_unchanged FAIL\n"
  set $fails = $fails + 1
end
if CurrentTCB == &runner && NextTCB == g_next_before
  printf "GDBCHK delay_current_next_unchanged PASS\n"
else
  printf "GDBCHK delay_current_next_unchanged FAIL\n"
  set $fails = $fails + 1
end
if DelayList.Head == g_delay_before.Head && DelayList.Tail == g_delay_before.Tail
  printf "GDBCHK delay_wait_queue_unchanged PASS\n"
else
  printf "GDBCHK delay_wait_queue_unchanged FAIL\n"
  set $fails = $fails + 1
end
if g_PrioMap == g_bitmap_before && g_SystemTickCount == g_tick_before
  printf "GDBCHK delay_bitmap_and_tick_unchanged PASS\n"
else
  printf "GDBCHK delay_bitmap_and_tick_unchanged FAIL\n"
  set $fails = $fails + 1
end
if g_CriticalNesting == g_nesting_before && $primask == g_mask_before
  printf "GDBCHK delay_mask_and_nesting_unchanged PASS\n"
else
  printf "GDBCHK delay_mask_and_nesting_unchanged FAIL\n"
  set $fails = $fails + 1
end
if ($xpsr & 0x1ff) == g_expected_ipsr
  printf "GDBCHK delay_real_exception_context PASS\n"
else
  printf "GDBCHK delay_real_exception_context FAIL\n"
  set $fails = $fails + 1
end
if (*(unsigned int *)0xe000ed04 & 0x14000000) == g_pending_before
  printf "GDBCHK delay_pending_exceptions_unchanged PASS\n"
else
  printf "GDBCHK delay_pending_exceptions_unchanged FAIL\n"
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

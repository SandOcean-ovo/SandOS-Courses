# ============================================================================
# neg_never_hit.gdb —— 负例 GDBNEG-02：断点永远不会命中
#
# 在一个程序永远到不了的地址下断点。预期：GDB 不返回，run_gdb.py 超时（退出码 2）。
# 目的：证明“没跑到检查点”不会被当作通过。
# ============================================================================
set pagination off
set confirm off
break *0x00ff0000
continue
printf "GDBCHK should_not_get_here PASS\n"
printf "GDBRESULT PASS\n"
kill
quit

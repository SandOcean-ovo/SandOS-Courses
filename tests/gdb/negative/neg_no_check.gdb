# ============================================================================
# neg_no_check.gdb —— 负例 GDBNEG-03：脚本正常结束，但没有输出任何检查结果
#
# 预期：run_gdb.py 判失败（退出码 1，"no GDBCHK lines"）。
# 目的：证明“空通过”（什么都没检查却返回成功）不会发生。
# ============================================================================
set pagination off
set confirm off
printf "GDBINFO connected only\n"
kill
quit

<#
.SYNOPSIS
构建并运行阶段 0 Keil 软件仿真探针，检查真实日志，不使用真机下载驱动。
.PARAMETER KeilPath
本机 UV4.exe 的路径，默认沿用已验证安装。
.PARAMETER ExpectedTicks
期望 tick 次数，默认为 3。传 4 作为运行器负例，应退出 1，不能计为探针通过。
.NOTES
仅停止本脚本创建的进程；每一步最多 30 秒。清除当前探针的旧日志避免假通过。
#>
param([string]$KeilPath = 'C:/Keil_v5/UV4/UV4.exe', [int]$ExpectedTicks = 3)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskDir = Join-Path $taskRoot 'ports/stm32f103-keil/environment-probe'
$taskBuild = Join-Path $taskDir 'build'
New-Item -ItemType Directory -Force $taskBuild | Out-Null

function Invoke-ProbeProcess([string[]]$Arguments) {
    # 独立隐藏会话与有界等待；绝不按进程名批量终止用户已有 Keil。
    $taskOwned = Start-Process -FilePath $KeilPath -ArgumentList $Arguments -WorkingDirectory $taskDir -PassThru -WindowStyle Hidden
    if (-not $taskOwned.WaitForExit(30000)) {
        Stop-Process -Id $taskOwned.Id -ErrorAction SilentlyContinue
        throw 'Keil probe timeout (30 s)'
    }
    if ($taskOwned.ExitCode -ne 0) { throw "Keil process exit: $($taskOwned.ExitCode)" }
}
try {
    if (-not (Test-Path -LiteralPath $KeilPath -PathType Leaf)) { throw "Keil not found: $KeilPath" }
    # 仅移除本探针固定文件；不是清理整个用户工作区。
    foreach ($taskName in @('simulator.log','build.log','session.log','probe.axf')) {
        $taskOld = Join-Path $taskBuild $taskName
        if (Test-Path -LiteralPath $taskOld) { Remove-Item -LiteralPath $taskOld }
    }
    Invoke-ProbeProcess @('-r', "`"$taskDir/probe.uvprojx`"", '-j0', '-o', "`"$taskBuild/build.log`"")
    $taskBuildText = [IO.File]::ReadAllText((Join-Path $taskBuild 'build.log'))
    if ($taskBuildText -notmatch '0 Error\(s\), 0 Warning\(s\)' -or -not (Test-Path (Join-Path $taskBuild 'probe.axf'))) { throw 'Build did not produce a clean fresh image' }
    Invoke-ProbeProcess @('-d', "`"$taskDir/probe.uvprojx`"", '-j0', '-l', "`"$taskBuild/session.log`"")
    $taskLog = [IO.File]::ReadAllText((Join-Path $taskBuild 'simulator.log'))
    $taskExpected = "(?m)^ENV-KEIL-01 started=AC500001 done=600D0001 ticks=$ExpectedTicks pendsv=3 thread=0 tick_irq=15 pendsv_irq=14\r?$"
    if ($taskLog -match '(?im)\*\*\* error|access violation' -or $taskLog -notmatch $taskExpected) { throw 'Missing or unexpected exception results' }
    if ($taskLog -notmatch '(?m)^MEM 12345678 A5A5A5A5 00000528 EDCBA987\r?$' -or $taskLog -notmatch '(?m)^END-KEIL-01\r?$') { throw 'Memory values or end marker missing' }
    $taskRegisters = [regex]::Match($taskLog,'(?m)^REG pc=([0-9A-F]{8}) sp=([0-9A-F]{8}) xpsr=([0-9A-F]{8})\r?$')
    if (-not $taskRegisters.Success) { throw 'Register observation missing' }
    $taskPc = [Convert]::ToUInt32($taskRegisters.Groups[1].Value,16)
    $taskSp = [Convert]::ToUInt32($taskRegisters.Groups[2].Value,16)
    if ($taskPc -lt 0x08000000 -or $taskPc -ge 0x08010000 -or $taskSp -le 0x20000000 -or $taskSp -gt 0x20005000 -or ($taskSp % 8) -ne 0) { throw 'Unexpected PC/SP range or stack alignment' }
    Write-Output 'ENV-KEIL-01 PASS (startup, 3 SysTick, 3 PendSV, exception numbers, memory, registers)'
    exit 0
} catch {
    Write-Output "ENV-KEIL-01 FAIL: $($_.Exception.Message)"
    exit 1
}

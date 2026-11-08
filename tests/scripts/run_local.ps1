<#
.SYNOPSIS
使用本机已验证工具运行完整无板回归，不修改系统 PATH。
.DESCRIPTION
仅为现有 run_all.sh 提供 Windows 入口，不引入第二套构建逻辑。
默认工具目录对应阶段 0 本机版本清单，可通过参数指定其他安装位置。
缺少工具立即失败；测试退出码原样返回，不把启动失败当作通过。
.PARAMETER GnuBin
包含 arm-none-eabi-gcc 和 arm-none-eabi-gdb 的目录。
.PARAMETER QemuBin
包含 qemu-system-arm 和主机 gcc 的 MSYS2 UCRT64 目录。
.PARAMETER MakeBin
包含 mingw32-make 的目录。
.PARAMETER BashPath
Git for Windows 的 bash.exe；避免误用 Windows 的 WSL 启动入口。
.NOTES
仅临时设置当前进程 PATH，结束后恢复；不安装或分发工具。
#>
param(
    [string]$GnuBin = 'C:/ST/STM32CubeCLT_1.18.0/GNU-tools-for-STM32/bin',
    [string]$QemuBin = 'C:/msys64/ucrt64/bin',
    [string]$MakeBin = 'C:/msys64/mingw64/bin',
    [string]$BashPath = 'C:/Program Files/Git/bin/bash.exe'
)
$ErrorActionPreference = 'Stop'
$taskOldPath = $env:PATH
$taskOldTools = $env:SANDOS_TEST_TOOL_PATH
$taskExit = 1
Push-Location (Join-Path $PSScriptRoot '../..')
try {
    foreach ($taskTool in @(
        "$GnuBin/arm-none-eabi-gcc.exe", "$GnuBin/arm-none-eabi-gdb.exe",
        "$QemuBin/qemu-system-arm.exe", "$QemuBin/gcc.exe",
        "$MakeBin/mingw32-make.exe", $BashPath
    )) {
        if (-not (Test-Path -LiteralPath $taskTool -PathType Leaf)) {
            throw "缺少工具：$taskTool。请按阶段 0 版本清单检查路径。"
        }
    }
    # 主机测试所需的 UCRT64 DLL 必须与其 gcc 一同优先进入 PATH。
    $env:PATH = "$QemuBin;$MakeBin;$GnuBin;$taskOldPath"
    Get-Command python -ErrorAction Stop | Out-Null
    # Git 的 Bash 启动器会再次把自己的 mingw64/bin 放在 PATH 前面。
    # 必须在 Bash 内恢复 UCRT64 优先级，否则新编译 cc1 的 DLL 搜索可能失败。
    # 工具路径通过环境变量传递，不把用户路径拼接为可执行 shell 代码。
    $env:SANDOS_TEST_TOOL_PATH = "$QemuBin;$MakeBin;$GnuBin"
    & $BashPath -c 'export PATH="$(cygpath -u -p "$SANDOS_TEST_TOOL_PATH"):$PATH"; source "$0"' tests/scripts/run_all.sh
    $taskExit = $LASTEXITCODE
} finally {
    $env:PATH = $taskOldPath
    $env:SANDOS_TEST_TOOL_PATH = $taskOldTools
    Pop-Location
}
exit $taskExit

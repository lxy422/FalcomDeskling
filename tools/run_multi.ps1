# 多角色回归：一次起好几个人，跑几秒看 stats 里 n= 是不是对得上、
# 每个人是不是都有自己的 rect 和 armed 状态
param(
    [string]$Skins = "科洛丝,金,提妲",
    [int]$Seconds = 6,
    [string]$Extra = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"
$errlog = Join-Path $root "build\bin\falcomdeskling_error.txt"

if (Test-Path $stats) { Remove-Item $stats -Force }
if (Test-Path $errlog) { Remove-Item $errlog -Force }

$args = @("--skin=$Skins", "--stats", "--tick=33")
if ($Extra) { $args += ($Extra -split ' ') }

$p = Start-Process -FilePath $exe -ArgumentList $args -PassThru
Start-Sleep -Seconds $Seconds
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

if (Test-Path $errlog) {
    Write-Host "---- error log ----"
    Get-Content $errlog
}
if (-not (Test-Path $stats)) {
    Write-Host "NO STATS FILE - process probably died early"
    exit 1
}
# 只留最后两拍：n= 那行 + 每个人一段
Write-Host "---- last stats ----"
Get-Content $stats -Tail 14

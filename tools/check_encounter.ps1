# Does the encounter table actually fire?
#
# The differential is the whole test: with --no-config there are no rules at
# all, MatchEncounter returns the built-in Ignore for everyone, so enc= must be
# 0. With config on, the same run must produce enc>0. That single number proves
# the whole chain ran: xml -> rules -> match -> emit -> counter.
#
# It also catches the failure mode where encounters silently never fire
# (distance bands wrong, everyone mapped to Ignore, the hostile-vs-hostile
# guard over-restricted).
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [int]$Seconds = 12,
    [int]$Runs = 2
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"

function Get-LastLine($extra) {
    Stop-Process -Name FalcomDeskling -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    if (Test-Path $stats) { Remove-Item $stats -Force }
    $args = @("--skin=all", "--stats", "--tick=33", "--nopresent")
    if ($extra) { $args += ($extra -split ' ') }
    $p = Start-Process -FilePath $exe -ArgumentList $args -PassThru
    for ($i = 0; $i -lt 30 -and -not (Test-Path $stats); $i++) { Start-Sleep -Milliseconds 200 }
    Start-Sleep -Seconds $Seconds
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400
    if (-not (Test-Path $stats)) { return $null }
    return (Get-Content $stats -Encoding Unicode | Select-String '^t=' | Select-Object -Last 1).ToString()
}

function Get-Enc($line) {
    if ($line -notmatch 'enc=(\d+)') { return -1 }
    return [int]$matches[1]
}

Write-Host "---- config on ----"
$onBest = 0
for ($r = 1; $r -le $Runs; $r++) {
    $l = Get-LastLine ""
    if ($null -eq $l) { Write-Host "  run $r NO STATS"; continue }
    $e = Get-Enc $l
    Write-Host ("  run {0}: enc={1}" -f $r, $e)
    if ($e -gt $onBest) { $onBest = $e }
}

Write-Host "---- config off (control) ----"
$offBest = 0
for ($r = 1; $r -le $Runs; $r++) {
    $l = Get-LastLine "--no-config"
    if ($null -eq $l) { Write-Host "  run $r NO STATS"; continue }
    $e = Get-Enc $l
    Write-Host ("  run {0}: enc={1}" -f $r, $e)
    if ($e -gt $offBest) { $offBest = $e }
}

Write-Host ""
Write-Host ("config on  best enc = {0}   (want > 0)" -f $onBest)
Write-Host ("config off best enc = {0}   (want 0)" -f $offBest)

$bad = 0
if ($onBest -le 0)  { Write-Host "FAIL: nobody ever acknowledged anybody with config on"; $bad++ }
if ($offBest -ne 0) { Write-Host "FAIL: --no-config still fired encounters"; $bad++ }
if ($bad -eq 0) { Write-Host "all ok" } else { exit 1 }

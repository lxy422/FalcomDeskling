# Does the state machine actually run, and does --state= really pin it?
#
# Two claims, tested separately because they fail differently:
#
#  1. Without --state, the table must be driving people around: some entity
#     must be seen in a state other than walk/run. If everything stays
#     walk/run forever, the encounter -> chase path never reached the table.
#
#  2. With --state=chase, chg= must be 0 for every entity. If it moves at all,
#     the lock is only half-implemented (some path still calls SetState behind
#     the lock's back).
#
# The control matters: --no-config means no rules, so no EncounterNear/Far is
# ever emitted, so nobody should ever leave walk/run. That is the baseline the
# first claim is measured against.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [int]$Seconds = 10,
    [int]$Runs = 2
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"

# State enum order, straight out of config.h
$NAMES = @('walk', 'run', 'idle', 'chase', 'attack', 'beAttacked', 'stun', 'rest', 'dead', 'victory')

function RunOnce($extra) {
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
    return (Get-Content $stats -Encoding Unicode)
}

# Pull state= and chg= out of the per-entity lines
function ReadStates($lines) {
    $states = @{}
    $chg = @{}
    foreach ($l in $lines) {
        if ($l -notmatch '^\s+\[(.+?)\].*state=(\d+) chg=(\d+)') { continue }
        $name = $matches[1]
        $s = [int]$matches[2]
        $c = [long]$matches[3]
        if ($s -ge 0 -and $s -lt $NAMES.Count) { $states[$name] = $NAMES[$s] }
        if (-not $chg.ContainsKey($name)) { $chg[$name] = 0 }
        if ($c -gt $chg[$name]) { $chg[$name] = $c }
    }
    return [pscustomobject]@{ States = $states; Chg = $chg }
}

Write-Host "---- config on, no --state (table should be driving) ----"
$sawOther = @{}
$totalChg = 0
for ($r = 1; $r -le $Runs; $r++) {
    $lines = RunOnce ""
    if ($null -eq $lines) { Write-Host "  run $r NO STATS"; continue }
    $m = ReadStates $lines
    foreach ($k in $m.States.Keys) {
        if ($m.States[$k] -ne 'walk' -and $m.States[$k] -ne 'run') {
            $sawOther[$m.States[$k]] = 1
            Write-Host ("  run {0}: {1} is in {2}" -f $r, $k, $m.States[$k])
        }
        $totalChg += $m.Chg[$k]
    }
}
# Per-entity changes over the run. A healthy table settles; a table that is
# oscillating (chase->attack->chase every cooldown) keeps climbing. This is the
# number that caught the missing attackCoolSec.
$per = if ($Runs -and $totalChg) { [math]::Round($totalChg / ($Runs * 8), 1) } else { 0 }
Write-Host ("  distinct non-walk/run states seen: {0}   changes per entity per run: {1}" `
            -f $sawOther.Count, $per)

Write-Host "---- --state=chase (must not move) ----"
$bad = 0
for ($r = 1; $r -le $Runs; $r++) {
    $lines = RunOnce "--state=chase"
    if ($null -eq $lines) { Write-Host "  run $r NO STATS"; $bad++; continue }
    $m = ReadStates $lines
    foreach ($k in $m.States.Keys) {
        if ($m.States[$k] -ne 'chase') {
            Write-Host ("  run {0}: {1} escaped to {2}" -f $r, $k, $m.States[$k]); $bad++
        }
        if ($m.Chg[$k] -ne 0) {
            Write-Host ("  run {0}: {1} changed state {2} times while locked" `
                        -f $r, $k, $m.Chg[$k]); $bad++
        }
    }
    if ($bad -eq 0) { Write-Host ("  run {0}: all {1} pinned in chase" -f $r, $m.States.Count) }
}

Write-Host "---- --no-config control (nobody should leave walk/run) ----"
$drift = 0
$lines = RunOnce "--no-config"
if ($null -ne $lines) {
    $m = ReadStates $lines
    foreach ($k in $m.States.Keys) {
        if ($m.States[$k] -ne 'walk' -and $m.States[$k] -ne 'run') {
            Write-Host ("  {0} drifted into {1} with no rules" -f $k, $m.States[$k]); $drift++
        }
    }
}
Write-Host ("  drift with config off: {0}  (want 0)" -f $drift)

Write-Host ""
$fail = 0
if ($sawOther.Count -eq 0) {
    Write-Host "FAIL: nobody ever left walk/run - the table is not driving anything"; $fail++
}
# 10s at 33ms is ~300 ticks. More than ~1 change per 10 ticks means it is
# spinning between two states rather than behaving.
if ($per -gt 30) {
    Write-Host "FAIL: state churn is too high - the table is oscillating"; $fail++
}
if ($bad -ne 0) { Write-Host "FAIL: --state= did not hold"; $fail++ }
if ($drift -ne 0) { Write-Host "FAIL: states changed with no rules present"; $fail++ }
if ($fail -eq 0) { Write-Host "all ok" } else { exit 1 }

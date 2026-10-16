# Is the <react> layer wired, and is it the only thing firing it?
#
# --hit makes the first entity punch the next one every few seconds. The
# differential is the whole test:
#
#   without --hit  hits must be 0 everywhere   -> nothing else calls Hit()
#   with    --hit  some entity's hits must be > 0 -> the layer does run
#
# The control matters more than it looks. Hit() emits AttackedBy, which the
# built-in table routes to BeAttacked and back. If hits showed up without the
# switch, some other path would be firing the same event and the "one entry
# point" claim would be false.
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

# Biggest hits= across entities, plus which states were seen.
#
# Every line counts, not just the last snapshot. BeAttacked only lasts
# BEATTACKED_SEC (0.8s) and stats writes once per second, so reading a single
# instant catches it about a third of the time - that reads as "the state is
# never entered" when it is just being missed.
function ReadHits($lines) {
    $max = 0
    $states = @{}
    foreach ($l in $lines) {
        if ($l -notmatch 'state=(\d+) chg=\d+ hit=(\d+)') { continue }
        $s = [int]$matches[1]
        $h = [long]$matches[2]
        if ($h -gt $max) { $max = $h }
        if ($s -ge 0 -and $s -lt $NAMES.Count) { $states[$NAMES[$s]] = 1 }
    }
    return [pscustomobject]@{ Max = $max; States = $states.Keys }
}

Write-Host "---- --hit (the layer should fire) ----"
$best = 0
$sawBeAttacked = $false
for ($r = 1; $r -le $Runs; $r++) {
    $m = RunOnce "--hit"
    if ($null -eq $m) { Write-Host "  run $r NO STATS"; continue }
    $x = ReadHits $m
    Write-Host ("  run {0}: max hits={1}  states={2}" -f $r, $x.Max, ($x.States -join ','))
    if ($x.Max -gt $best) { $best = $x.Max }
    if ($x.States -contains 'beAttacked') { $sawBeAttacked = $true }
}

Write-Host "---- control, no --hit (nothing should fire) ----"
$ctrl = 0
for ($r = 1; $r -le $Runs; $r++) {
    $m = RunOnce ""
    if ($null -eq $m) { Write-Host "  run $r NO STATS"; continue }
    $x = ReadHits $m
    Write-Host ("  run {0}: max hits={1}" -f $r, $x.Max)
    if ($x.Max -gt $ctrl) { $ctrl = $x.Max }
}

Write-Host ""
Write-Host ("--hit max hits = {0}   (want > 0)" -f $best)
Write-Host ("control max hits = {0}   (want 0)" -f $ctrl)
Write-Host ("beAttacked seen during --hit: {0}" -f $sawBeAttacked)

$bad = 0
if ($best -le 0) {
    Write-Host "FAIL: --hit produced no hits - the react layer is not wired"; $bad++
}
if ($ctrl -ne 0) {
    Write-Host "FAIL: hits fired without --hit - something else calls Hit()"; $bad++
}
if (-not $sawBeAttacked) {
    Write-Host "FAIL: hits happened but nobody ever entered beAttacked"; $bad++
}
if ($bad -eq 0) { Write-Host "all ok" } else { exit 1 }

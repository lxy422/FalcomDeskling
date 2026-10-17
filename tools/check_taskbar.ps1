# Taskbar stroll mode.
#
# Five claims, each failing differently, so each gets its own check:
#
#  1. only one entity survives. The mode is "one person on the bar" - if eight
#     stay up, the pop loop never ran.
#  2. y never changes. The whole point is a single horizontal line; drifting
#     vertically means it is still using the walk-around clamp.
#  3. x moves, and reverses at both ends. Two observed turns prove the edges
#     are real and not just a random walk.
#  4. slow. Under a walk-speed threshold, so a regression to normal walking
#     shows up as a number, not as "hmm it looks fast".
#  5. no mouse interaction. The mouse sits wherever it was; if the flee hysteresis
#     were still running we would see run state or dir flips toward it.
#
# Poses and emotes are probabilistic, so they are reported, not asserted -
# a single run that happens not to pose is not a bug.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [int]$Seconds = 12,
    [int]$TurnSeconds = 60
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"

function CountTurns($lines) {
    $lastX = $null
    $lastSign = 0
    $turns = 0
    foreach ($line in $lines) {
        if ($line -notmatch 'pos=(-?[\d.]+),(-?[\d.]+)') { continue }
        $x = [double]$matches[1]
        if ($null -ne $lastX) {
            $delta = $x - $lastX
            if ([math]::Abs($delta) -ge 0.5) {
                $sign = [math]::Sign($delta)
                if ($lastSign -ne 0 -and $sign -ne $lastSign) { $turns++ }
                $lastSign = $sign
            }
        }
        $lastX = $x
    }
    return $turns
}

function RunTaskbar($extra, $secs, $targetTurns = 0) {
    # Stats are shared, so refuse concurrent instances instead of killing them.
    if (Get-Process -Name FalcomDeskling -ErrorAction SilentlyContinue) {
        throw 'Close existing FalcomDeskling instances before running this test.'
    }
    if (Test-Path $stats) { Remove-Item $stats -Force }
    $runArgs = @("--skin=all", "--stats", "--tick=33", "--taskbar")
    if ($extra) { $runArgs += ($extra -split ' ') }
    $p = Start-Process -FilePath $exe -ArgumentList $runArgs -PassThru -WindowStyle Hidden `
                       -WorkingDirectory (Join-Path $root "build\bin")
    try {
        for ($i = 0; $i -lt $secs; $i++) {
            Start-Sleep -Seconds 1
            if ($p.HasExited) { throw 'Taskbar process exited unexpectedly.' }
            if ($targetTurns -gt 0 -and (Test-Path $stats)) {
                $snapshot = Get-Content $stats -Encoding Unicode
                if ((CountTurns $snapshot) -ge $targetTurns) { break }
            }
        }
    } finally {
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    }
    if (-not (Test-Path $stats)) { return $null }
    return (Get-Content $stats -Encoding Unicode)
}

# ---- run A: default scale, checks the things that need a real speed ----
$lines = RunTaskbar "" $Seconds
if ($null -eq $lines) { Write-Host "NO STATS"; exit 1 }
Write-Host "---- run A: default scale ----"
$head = $lines | Select-String '^t=' | Select-Object -Last 1
$null = $head -match 'n=(\d+)'
$n = [int]$matches[1]
Write-Host ("entities on screen: {0}   (want 1)" -f $n)
$samples = @()
foreach ($l in $lines) {
    if ($l -notmatch 'state=(\d+) chg=\d+ hit=\d+ dir=(\d+) pos=(-?[\d.]+),(-?[\d.]+)') { continue }
    $samples += [pscustomobject]@{
        state = [int]$matches[1]; dir = [int]$matches[2]
        x = [double]$matches[3]; y = [double]$matches[4]
    }
}
if ($samples.Count -lt 3) { Write-Host "NOT ENOUGH SAMPLES"; exit 1 }
$ys = $samples | ForEach-Object { $_.y } | Sort-Object
$ySpread = $ys[-1] - $ys[0]
Write-Host ("y spread: {0:F0}px   (want 0 - one horizontal line)" -f $ySpread)
$xs = $samples | ForEach-Object { $_.x }
$path = 0.0
for ($i = 1; $i -lt $xs.Count; $i++) { $path += [math]::Abs($xs[$i] - $xs[$i-1]) }
$null = $head -match 't=([\d.]+)'
$secs = [double]$matches[1]
$pxSec = if ($secs -gt 0) { $path / $secs } else { 0 }
Write-Host ("travel: {0:F0}px over {1:F1}s = {2:F0}px/s   (want < 150 - slow)" `
            -f $path, $secs, $pxSec)
$states = $samples | ForEach-Object { $_.state } | Sort-Object -Unique
Write-Host ("states seen: {0}   (want only 0=walk)" -f ($states -join ','))
$badState = @($states | Where-Object { $_ -ne 0 }).Count
$poses = @($lines | Select-String 'emote=(?!-)[^\s]+').Count
Write-Host ("lines showing an emote up: {0}" -f $poses)

# ---- run B: observe two turns, with a bounded timeout ----
#
# The line is in physical pixels and does not care about --scale, but the leg
# speed does (it goes through _k). Blowing the scale up is the only honest way
# to make a 3840px traverse finish inside a test window - faking the turn would
# not prove the edge code works.
Write-Host ""
Write-Host "---- run B: --scale=800 so the edges are reachable ----"
$lines2 = RunTaskbar "--scale=800" $TurnSeconds 2
if ($null -eq $lines2) { Write-Host "NO STATS (run B)"; exit 1 }
$head2 = $lines2 | Select-String '^t=' | Select-Object -Last 1
$samples2 = @()
foreach ($l in $lines2) {
    if ($l -notmatch 'state=(\d+) chg=\d+ hit=\d+ dir=(\d+) pos=(-?[\d.]+),(-?[\d.]+)') { continue }
    $samples2 += [pscustomobject]@{ dir = [int]$matches[2]; x = [double]$matches[3] }
}
if ($samples2.Count -lt 3) { Write-Host "NOT ENOUGH SAMPLES (run B)"; exit 1 }
$x2 = $samples2 | ForEach-Object { $_.x }
$minX = ($x2 | Measure-Object -Minimum).Minimum
$maxX = ($x2 | Measure-Object -Maximum).Maximum
Write-Host ("x range: {0:F0} .. {1:F0}  span={2:F0}px" -f $minX, $maxX, ($maxX - $minX))
$turns = 0
$prev = $x2[0]
$lastSign = 0
for ($i = 1; $i -lt $x2.Count; $i++) {
    $d = $x2[$i] - $prev
    $prev = $x2[$i]
    if ([math]::Abs($d) -lt 0.5) { continue }
    $s = [math]::Sign($d)
    if ($lastSign -ne 0 -and $s -ne $lastSign) { $turns++ }
    $lastSign = $s
}
Write-Host ("direction reversals: {0}   (want >= 2 - both ends are real)" -f $turns)
# dir is 4=right, 0=left. A turn must show up here too, and if x and dir
# disagree the edge code is lying about which way it is facing
$dirs = $samples2 | ForEach-Object { $_.dir } | Sort-Object -Unique
Write-Host ("dirs seen: {0}   (want both 0 and 4)" -f ($dirs -join ','))
$sawBoth = ($dirs -contains 0) -and ($dirs -contains 4)

Write-Host ""
$bad = 0
if ($n -ne 1) { Write-Host "FAIL: more than one entity survived taskbar mode"; $bad++ }
if ($ySpread -gt 2) { Write-Host "FAIL: y drifted - not walking a single line"; $bad++ }
if ($pxSec -ge 150) { Write-Host "FAIL: moving at walk/run speed, not strolling"; $bad++ }
if ($badState -ne 0) { Write-Host "FAIL: left walk state - mouse or table still driving"; $bad++ }
if ($turns -lt 2) { Write-Host "FAIL: fewer than two reversals - edges not honoured"; $bad++ }
if (-not $sawBoth) { Write-Host "FAIL: never faced both ways"; $bad++ }
if ($bad -eq 0) { Write-Host "all ok" } else { exit 1 }

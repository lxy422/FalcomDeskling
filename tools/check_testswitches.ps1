# Do the "test each trigger separately" switches actually do what they claim?
#
# The point of --peer / --enc / --id is that a single encounter rule can be
# reproduced on demand. If any of them is a no-op, testing is still guesswork.
#
# The --id case is the interesting one: forcing everyone to 201 makes the whole
# table hostile-vs-hostile, and the guard in UpdateEncounter must skip those.
# So --id=201 must drive enc= down to 0. If it doesn't, that guard is not
# working and two monsters would chase each other.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [int]$Seconds = 8,
    [int]$Runs = 2
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"

# "Measure" is a built-in alias for Measure-Object, so this must not be called
# that or the call resolves to the cmdlet instead
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
    $l = (Get-Content $stats -Encoding Unicode | Select-String '^t=' | Select-Object -Last 1).ToString()
    $enc = -1; $n = -1
    if ($l -match 'enc=(\d+)') { $enc = [int]$matches[1] }
    if ($l -match 'n=(\d+)') { $n = [int]$matches[1] }
    return [pscustomobject]@{ Enc = $enc; N = $n }
}

function Best($extra, $label) {
    $best = 0
    $n = 0
    for ($r = 1; $r -le $Runs; $r++) {
        $m = RunOnce $extra
        if ($null -eq $m) { Write-Host "  $label run $r NO STATS"; continue }
        if ($m.Enc -gt $best) { $best = $m.Enc }
        $n = $m.N
    }
    Write-Host ("  {0,-24} n={1} best enc={2}" -f $label, $n, $best)
    return $best
}

Write-Host "---- baseline ----"
$base = Best "" "config default"

Write-Host "---- --enc=near / --enc=far (band is forced) ----"
# The assertion is not "more encounters" - forcing near can legitimately drop
# the count, because some rules say separate= at near and combat= at far.
# What must hold is that the switch has an effect at all, and that near and far
# disagree: that proves the band is what selects the rule, not a decorative flag.
$near = Best "--enc=near" "--enc=near"
$farr = Best "--enc=far" "--enc=far"

Write-Host "---- --id=201 (everyone becomes hostile class) ----"
$mon = Best "--id=201" "--id=201"

Write-Host "---- --peer (one extra named person) ----"
# Pick a real skin name out of the directory so this stays ASCII in the script
$name = (Get-ChildItem (Join-Path $root "build\bin\chr") -Directory |
         Where-Object { Test-Path (Join-Path $_.FullName "2.Walk\dir0.png") } |
         Select-Object -First 1).Name
if (-not $name) { Write-Host "  no skin found to use with --peer"; exit 1 }
$peer = Best "--peer=$name" "--peer=<first>"

Write-Host ""
Write-Host ("baseline={0}  near={1}  far={2}  id201={3}  peer={4}" -f $base, $near, $farr, $mon, $peer)

$bad = 0
# What must hold is that the two disagree: that proves the band is what picks
# the rule. Which one is bigger depends entirely on who happens to be on screen
# (the named rows say separate= at near and combat= at far for some people, so
# near=0 is correct behaviour for that pair, not a broken switch).
if ($near -eq $farr) {
    Write-Host "FAIL: --enc=near and --enc=far agree - band is not selecting the rule"; $bad++
}
if ($farr -le 0 -and $near -le 0) {
    Write-Host "FAIL: neither forced band fired anything"; $bad++
}
if ($mon -ne 0) {
    Write-Host "FAIL: --id=201 still fired encounters - hostile-vs-hostile guard broken"; $bad++
}
if ($peer -le 0) {
    Write-Host "FAIL: --peer produced no encounters"; $bad++
}
if ($bad -eq 0) { Write-Host "all ok" } else { exit 1 }

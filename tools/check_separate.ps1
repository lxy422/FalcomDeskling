# Does Separate() actually keep people apart?
#
# The proof is not "did they overlap once" but "how much total overlap is left
# at the end of a run". So this takes the LAST stats block, reads every entity's
# rect=, and sums the pairwise intersection area. Then it compares against what
# the same run looks like with the push disabled, which is what --noseparate is
# for.
#
# peer= on the t= line counts the ticks where a flee direction was changed
# purely to route around a peer, so it is the "they are actively steering around
# each other" number.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [int]$Seconds = 20,
    [string]$Skins = "all",
    [int]$MaxEntities = 8
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"

function Measure-Run($extra) {
    Stop-Process -Name FalcomDeskling -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    if (Test-Path $stats) { Remove-Item $stats -Force }

    $args = @("--skin=$Skins", "--stats", "--tick=33", "--max=$MaxEntities")
    if ($extra) { $args += ($extra -split ' ') }
    $p = Start-Process -FilePath $exe -ArgumentList $args -PassThru
    for ($i = 0; $i -lt 30 -and -not (Test-Path $stats); $i++) { Start-Sleep -Milliseconds 200 }
    Start-Sleep -Seconds $Seconds
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500

    if (-not (Test-Path $stats)) { return $null }
    $lines = Get-Content $stats -Encoding Unicode

    # What we want is "are two people standing on the same spot". The rect= in
    # the log is NOT that: it is the padded global bbox, hundreds of pixels on a
    # side, so two people standing far apart still have overlapping rects. The
    # honest number is the distance between their feet anchors, which pos= gives
    # directly.
    #
    # Average over EVERY block, not just the last one: a single snapshot is
    # dominated by luck.
    $sumMin = [double]0
    $sumMean = [double]0
    $stacked = 0
    $blocks = 0
    $n = 0
    $lastPeer = 0
    $series = @()
    $cur = @()
    foreach ($l in $lines) {
        if ($l -match '^t=') {
            if ($l -match 'peer=(\d+)') { $lastPeer = [int]$matches[1] }
            $found = @()
            foreach ($r in $cur) {
                if ($r -match 'pos=(-?[\d.]+),(-?[\d.]+)') {
                    $found += ,@([double]$matches[1], [double]$matches[2])
                }
            }
            if ($found.Count -gt 1) {
                $mn = 1e300; $tot = [double]0; $cnt = 0; $close = 0
                for ($i = 0; $i -lt $found.Count; $i++) {
                    for ($j = $i + 1; $j -lt $found.Count; $j++) {
                        $dx = $found[$i][0] - $found[$j][0]
                        $dy = $found[$i][1] - $found[$j][1]
                        $d = [Math]::Sqrt($dx * $dx + $dy * $dy)
                        if ($d -lt $mn) { $mn = $d }
                        $tot += $d; $cnt += 1
                        if ($d -lt 60.0) { $close += 1 }
                    }
                }
                $sumMin += $mn
                $sumMean += ($tot / $cnt)
                $stacked += $close
                $blocks += 1
                $n = $found.Count
                $series += [int]$mn
            }
            $cur = @($l)
        } else {
            $cur += $l
        }
    }
    $found = @()
    foreach ($r in $cur) {
        if ($r -match 'pos=(-?[\d.]+),(-?[\d.]+)') {
            $found += ,@([double]$matches[1], [double]$matches[2])
        }
    }
    if ($found.Count -gt 1) {
        $mn = 1e300; $tot = [double]0; $cnt = 0; $close = 0
        for ($i = 0; $i -lt $found.Count; $i++) {
            for ($j = $i + 1; $j -lt $found.Count; $j++) {
                $dx = $found[$i][0] - $found[$j][0]
                $dy = $found[$i][1] - $found[$j][1]
                $d = [Math]::Sqrt($dx * $dx + $dy * $dy)
                if ($d -lt $mn) { $mn = $d }
                $tot += $d; $cnt += 1
                if ($d -lt 60.0) { $close += 1 }
            }
        }
        $sumMin += $mn
        $sumMean += ($tot / $cnt)
        $stacked += $close
        $blocks += 1
        $n = $found.Count
        $series += [int]$mn
    }
    if ($blocks -eq 0) { return $null }
    return [pscustomobject]@{
        N = $n
        Blocks = $blocks
        MinDist = [int]($sumMin / $blocks)
        MeanDist = [int]($sumMean / $blocks)
        Stacked = [int]($stacked / $blocks)
        Peer = $lastPeer
        Series = $series
    }
}

# --stack puts all eight on the same foot. Spread out over a normal desktop
# they never get close enough for SeparateFrom to fire, so a plain run proves
# nothing either way. Starting them stacked is the only way to see the push.
Write-Host "---- stacked, Separate() ON ----"
$on = Measure-Run "--stack"
if ($null -eq $on) { Write-Host "NO STATS"; exit 1 }
Write-Host ("n={0} blocks={1} closest={2}px mean={3}px stacked(<60px)={4}/block peer-ticks={5}" -f `
    $on.N, $on.Blocks, $on.MinDist, $on.MeanDist, $on.Stacked, $on.Peer)
Write-Host ("   first blocks closest: " + ($on.Series -join " "))

Write-Host "---- stacked, Separate() OFF (control) ----"
$off = Measure-Run "--stack --noseparate"
if ($null -eq $off) {
    Write-Host "NO STATS - is --noseparate wired up?"
    exit 1
}
Write-Host ("n={0} blocks={1} closest={2}px mean={3}px stacked(<60px)={4}/block peer-ticks={5}" -f `
    $off.N, $off.Blocks, $off.MinDist, $off.MeanDist, $off.Stacked, $off.Peer)
Write-Host ("   first blocks closest: " + ($off.Series -join " "))

# The number that matters: how far apart the closest pair ends up being. If the
# push works, "on" should separate them faster and stay further apart.
Write-Host ("closest-pair distance: on={0}px off={1}px  (mean over {2}/{3} blocks)" -f `
    $on.MinDist, $off.MinDist, $on.Blocks, $off.Blocks)
Write-Host ("how fast they split from the same spot (first 6 blocks, px):")
Write-Host ("  ON  " + (($on.Series | Select-Object -First 6) -join " "))
Write-Host ("  OFF " + (($off.Series | Select-Object -First 6) -join " "))
if ($off.MinDist -gt 0) {
    $gain = (([double]$on.MinDist / [double]$off.MinDist) - 1.0) * 100.0
    Write-Host ("closest pair is {0:F1}% further apart with Separate() on" -f $gain)
}

# Compare the per-frame alpha bboxes in falcomdeskling_dump.txt against the
# global crop= that the runtime actually uses. If any art set (esp. the weapon
# ones) sticks out of the global box, it gets silently clipped.
#
# NOTE: keep this file ASCII-only. PowerShell 5.1 decodes .ps1 with the system
# codepage, so UTF-8 Chinese in a script without a BOM breaks the parser.
param(
    [string]$Dump = "build\bin\falcomdeskling_dump.txt",
    [string]$Stats = "build\bin\falcomdeskling_stats.txt"
)

$lines = Get-Content $Dump -Encoding Unicode

$minX = 100000; $minY = 100000; $maxX = -1; $maxY = -1
$perFolder = @{}
$folder = ""
foreach ($l in $lines) {
    if ($l -match '^(1\.Run|2\.Walk|4\.standby|5\.run2)\\dir') { $folder = $matches[1] }
    if ($l -match 'f\d+\s+x\[(\d+)\.\.(\d+)\]\s+y\[(\d+)\.\.(\d+)\]') {
        $x0 = [int]$matches[1]; $x1 = [int]$matches[2]
        $y0 = [int]$matches[3]; $y1 = [int]$matches[4]
        if ($x1 -lt $x0) { continue }   # fully transparent frame, empty bbox
        if ($folder) {
            if (-not $perFolder.ContainsKey($folder)) { $perFolder[$folder] = @(100000,100000,-1,-1) }
            $a = $perFolder[$folder]
            if ($x0 -lt $a[0]) { $a[0] = $x0 }
            if ($x1 -gt $a[2]) { $a[2] = $x1 }
            if ($y0 -lt $a[1]) { $a[1] = $y0 }
            if ($y1 -gt $a[3]) { $a[3] = $y1 }
            $perFolder[$folder] = $a
        }
        if ($x0 -lt $minX) { $minX = $x0 }
        if ($x1 -gt $maxX) { $maxX = $x1 }
        if ($y0 -lt $minY) { $minY = $y0 }
        if ($y1 -gt $maxY) { $maxY = $y1 }
    }
}

Write-Host ("union of all frames in dump: x[{0}..{1}] y[{2}..{3}] = {4}x{5}" -f `
    $minX, $maxX, $minY, $maxY, ($maxX - $minX + 1), ($maxY - $minY + 1))
foreach ($k in ($perFolder.Keys | Sort-Object)) {
    $a = $perFolder[$k]
    Write-Host ("  {0,-10} x[{1}..{2}] y[{3}..{4}]" -f $k, $a[0], $a[2], $a[1], $a[3])
}

if (-not (Test-Path $Stats)) {
    Write-Host "  no stats file next to the dump; union only"
    return
}

$crop = $null
foreach ($l in (Get-Content $Stats -Encoding Unicode)) {
    if ($l -match 'crop=(\d+),(\d+) (\d+)x(\d+)') { $crop = $matches }
}
if (-not $crop) {
    Write-Host "  no crop= line in stats; run with --stats first"
    return
}
$cx = [int]$crop[1]; $cy = [int]$crop[2]; $cw = [int]$crop[3]; $ch = [int]$crop[4]
Write-Host ("global box from stats: crop={0},{1} {2}x{3} -> x[{4}..{5}] y[{6}..{7}]" -f `
    $cx, $cy, $cw, $ch, $cx, ($cx + $cw - 1), $cy, ($cy + $ch - 1))

$bad = @()
if ($minX -lt $cx) { $bad += "LEFT" }
if ($maxX -gt ($cx + $cw - 1)) { $bad += "RIGHT" }
if ($minY -lt $cy) { $bad += "TOP" }
if ($maxY -gt ($cy + $ch - 1)) { $bad += "BOTTOM" }
if ($bad.Count -eq 0) {
    Write-Host "  OK: global box contains every frame in the dump"
} else {
    Write-Host ("  [!] clipped on: {0}" -f ($bad -join "/"))
}

# For every skin folder: dump the per-frame bboxes, run it armed with the
# surface probe, and compare union vs global crop. One line per skin.
#
# Keep ASCII-only: PS 5.1 reads .ps1 in the system codepage.
param([int]$Seconds = 4)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$dump = Join-Path $root "build\bin\falcomdeskling_dump.txt"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"
$chr = "C:\Users\lxy_42\source\repos\WpfTest\Resource\Chr"

foreach ($name in (Get-ChildItem $chr -Directory | Sort-Object Name)) {
    $n = $name.Name
    $hasWeapon = (Test-Path (Join-Path $name.FullName "4.standby\dir0.png")) -and
                 (Test-Path (Join-Path $name.FullName "5.run2\dir0.png"))

    if (Test-Path $dump) { Remove-Item $dump -Force }
    if (Test-Path $stats) { Remove-Item $stats -Force }

    Start-Process -FilePath $exe -ArgumentList @("--skin=$n", "--dumpcrop") -Wait
    $p = Start-Process -FilePath $exe -ArgumentList @("--skin=$n", "--armed", "--stats", "--surfaceprobe", "--tick=33") -PassThru
    Start-Sleep -Seconds $Seconds
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200

    $lines = Get-Content $dump -Encoding Unicode
    $minX = 100000; $minY = 100000; $maxX = -1; $maxY = -1
    foreach ($l in $lines) {
        if ($l -match 'f\d+\s+x\[(\d+)\.\.(\d+)\]\s+y\[(\d+)\.\.(\d+)\]') {
            $x0 = [int]$matches[1]; $x1 = [int]$matches[2]
            $y0 = [int]$matches[3]; $y1 = [int]$matches[4]
            if ($x1 -lt $x0) { continue }
            if ($x0 -lt $minX) { $minX = $x0 }
            if ($x1 -gt $maxX) { $maxX = $x1 }
            if ($y0 -lt $minY) { $minY = $y0 }
            if ($y1 -gt $maxY) { $maxY = $y1 }
        }
    }

    $crop = $null
    if (Test-Path $stats) {
        foreach ($l in (Get-Content $stats -Encoding Unicode)) {
            if ($l -match 'crop=(\d+),(\d+) (\d+)x(\d+)') { $crop = $matches }
        }
    }

    if (-not $crop) {
        Write-Host ("{0,-10} weapon={1}  NO STATS / crop missing" -f $n, $hasWeapon)
        continue
    }
    $cx = [int]$crop[1]; $cy = [int]$crop[2]; $cw = [int]$crop[3]; $ch = [int]$crop[4]
    $bad = @()
    if ($minX -lt $cx) { $bad += "LEFT" }
    if ($maxX -gt ($cx + $cw - 1)) { $bad += "RIGHT" }
    if ($minY -lt $cy) { $bad += "TOP" }
    if ($maxY -gt ($cy + $ch - 1)) { $bad += "BOTTOM" }
    $verdict = if ($bad.Count -eq 0) { "ok" } else { ("CLIPPED:" + ($bad -join "/")) }
    Write-Host ("{0,-10} weapon={1} union=x[{2}..{3}]y[{4}..{5}] crop={6},{7} {8}x{9}  {10}" -f `
        $n, $hasWeapon, $minX, $maxX, $minY, $maxY, $cx, $cy, $cw, $ch, $verdict)
}

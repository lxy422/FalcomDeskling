# Read the last stats sample and report, per entity, whether the surface probe
# shows content touching a window edge. first==0 or last==winH-1 means the
# drawing is butting right up against the window border, i.e. something is
# being clipped by the window rather than by the crop box.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [string]$Stats = "build\bin\falcomdeskling_stats.txt",
    [int]$Tail = 1
)

$lines = Get-Content $Stats -Encoding Unicode
$heads = @($lines | Where-Object { $_ -match '^t=' })
$last = $heads | Select-Object -Last $Tail
$idx = [array]::IndexOf($lines, $last)

Write-Host $last
for ($i = $idx + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^t=') { break }
    if ($lines[$i] -match '^\s{2}\[(.+?)\]') {
        $name = $matches[1]
        $surf = ""
        for ($j = $i + 1; $j -lt [Math]::Min($i + 4, $lines.Count); $j++) {
            if ($lines[$j] -match 'surf win=(\d+)x(\d+)\s+first=(-?\d+)\s+last=(-?\d+)') {
                $surf = $matches
                break
            }
        }
        if (-not $surf) { continue }
        $w = [int]$surf[1]; $h = [int]$surf[2]; $f = [int]$surf[3]; $l = [int]$surf[4]
        $flag = ""
        if ($f -le 0) { $flag += " TOUCH_TOP" }
        if ($l -ge $h - 1) { $flag += " TOUCH_BOTTOM" }
        if ($flag -eq "") { $flag = " ok" }
        Write-Host ("  {0,-10} win={1}x{2} first={3} last={4} margin=({5},{6}){7}" -f `
            $name, $w, $h, $f, $l, $f, ($h - 1 - $l), $flag)
    }
}

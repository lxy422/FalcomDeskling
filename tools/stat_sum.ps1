# 把 stats 里每一拍的 n= 和每个角色的 rect 汇总成一眼能看的表
param([string]$Path = "build\bin\falcomdeskling_stats.txt")

$lines = Get-Content $Path -Encoding Unicode
$heads = @($lines | Where-Object { $_ -match '^t=' })
Write-Host ("samples={0}" -f $heads.Count)
foreach ($h in $heads) { Write-Host $h }

# 最后一拍里每个角色的行
$last = ($heads | Select-Object -Last 1)
if ($last) {
    $idx = [array]::IndexOf($lines, $last)
    Write-Host "---- last sample entities ----"
    for ($i = $idx + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^t=') { break }
        if ($lines[$i] -match '^\s{2}\[') {
            # 角色行 + 后面两行（blit / rect）
            $name = $lines[$i]
            $rect = ""
            for ($j = $i + 1; $j -lt [Math]::Min($i + 4, $lines.Count); $j++) {
                if ($lines[$j] -match 'rect=') { $rect = $lines[$j].Trim(); break }
            }
            Write-Host ($name.Trim() + "  " + $rect)
        }
    }
}

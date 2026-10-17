param([int]$Seconds = 18)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $repo 'build\bin'
$exe = Join-Path $bin 'FalcomDeskling.exe'
$stats = Join-Path $bin 'falcomdeskling_stats.txt'
if (Get-Process FalcomDeskling -ErrorAction SilentlyContinue) { throw 'Close existing instances before testing the shared stats output.' }
$original = if (Test-Path -LiteralPath $stats) { [IO.File]::ReadAllBytes($stats) } else { $null }
try {
    foreach ($mode in @('desktop', 'taskbar')) {
        if (Test-Path -LiteralPath $stats) { Remove-Item -LiteralPath $stats }
        $runArgs = @('--stats', '--enemy1=1', '--enemy2=2', '--enemyauto=2', '--wander')
        if ($mode -eq 'taskbar') { $runArgs += '--taskbar' }
        $p = Start-Process -FilePath $exe -ArgumentList $runArgs -WorkingDirectory $repo -PassThru -WindowStyle Hidden
        try {
            Start-Sleep -Seconds $Seconds
            if ($p.HasExited) { throw "$mode process exited unexpectedly." }
        } finally { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
        if (-not (Test-Path -LiteralPath $stats)) { throw "$mode did not generate stats." }
        $lines = Get-Content -LiteralPath $stats -Encoding Unicode
        Copy-Item -LiteralPath $stats -Destination (Join-Path $repo "build\battle-live-$mode.txt")
        $rows = @()
        $footY = @()
        foreach ($line in $lines) {
            if ($line -match 'combat name=.+ faction=(chr|Enemy1|Enemy2) entity=(\d+) target=(\d+) hp=(\d+)/(\d+) down=(\d+) entering=(\d+) attacks=(\d+)') {
                $rows += [pscustomobject]@{Camp=$matches[1];Id=[long]$matches[2];Target=[long]$matches[3];Hp=[int]$matches[4];Max=[int]$matches[5];Down=[int]$matches[6];Entry=[int]$matches[7];Attacks=[int]$matches[8]}
            }
            if ($line -match 'state=\d+ chg=\d+ hit=\d+ dir=\d+ pos=-?[\d.]+,(-?[\d.]+)') { $footY += [double]$matches[1] }
        }
        if ($rows.Count -lt 4) { throw "$mode missing combat samples." }
        foreach ($camp in @('chr', 'Enemy1', 'Enemy2')) {
            if (-not @($rows | Where-Object Camp -eq $camp).Count) { throw "$mode missing camp $camp" }
        }
        $identities = @{}
        foreach ($row in $rows) { $identities[$row.Id] = $row.Camp }
        foreach ($row in $rows) {
            $expected = if ($row.Camp -eq 'Enemy2') { 5 } else { 10 }
            if ($row.Max -ne $expected -or $row.Hp -lt 0 -or $row.Hp -gt $expected) { throw "$mode invalid HP" }
            if ($row.Target -gt 0 -and $identities[$row.Target] -eq $row.Camp) { throw "$mode targeted friendly" }
            if (($row.Down -or $row.Entry) -and $row.Target -gt 0) { throw "$mode inactive entity has a target" }
        }
        $attacks = ($rows | Measure-Object Attacks -Maximum).Maximum
        $damaged = @($rows | Where-Object { $_.Hp -lt $_.Max }).Count
        $enemyIds = @($rows | Where-Object Camp -ne 'chr' | Select-Object -ExpandProperty Id -Unique).Count
        if ($attacks -lt 1 -or $damaged -lt 1) { throw "$mode did not perform automatic combat." }
        if ($enemyIds -lt 4) { throw "$mode automatic spawning did not add enemies." }
        if ($mode -eq 'taskbar') {
            $spread = ($footY | Measure-Object -Maximum).Maximum - ($footY | Measure-Object -Minimum).Minimum
            if ($spread -gt 2) { throw "Taskbar vertical drift: $spread" }
        }
        Write-Output "PASS $mode : $($rows.Count) samples, $enemyIds enemy instances, max attacks=$attacks, damaged samples=$damaged"
    }
} finally {
    if ($null -ne $original) { [IO.File]::WriteAllBytes($stats, $original) }
}

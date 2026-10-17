# Verify the ZIP as delivered, in a fresh path containing spaces.
param([Parameter(Mandatory = $true)][string]$Zip)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$unpack = Join-Path $repo ('build\portable smoke ' + [guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $Zip -DestinationPath $unpack
$stage = Get-ChildItem -LiteralPath $unpack -Directory | Select-Object -First 1
if (-not $stage) { throw 'Archive has no application directory.' }
if (Get-ChildItem -LiteralPath $stage.FullName -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
    throw 'Portable package contains junctions or symlinks.'
}
$count = 0
foreach ($line in Get-Content -LiteralPath (Join-Path $stage.FullName 'SHA256SUMS.txt') -Encoding UTF8) {
    if ($line -notmatch '^([A-F0-9]{64})  (.+)$') { throw 'Invalid checksum manifest.' }
    $expected = $matches[1]
    $file = [IO.Path]::GetFullPath((Join-Path $stage.FullName $matches[2]))
    if (-not $file.StartsWith($stage.FullName + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Checksum path escapes application directory.'
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expected) { throw "Checksum mismatch: $file" }
    $count++
}
$exe = Join-Path $stage.FullName 'FalcomDeskling.exe'
$p = Start-Process -FilePath $exe -ArgumentList @('--dumpconfig', '--skin=all') -WorkingDirectory $repo -WindowStyle Hidden -PassThru
try {
    if (-not $p.WaitForExit(15000)) { throw 'Portable configuration dump timed out.' }
    if ($p.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $stage.FullName 'falcomdeskling_config.txt'))) {
        throw 'Portable resource/config loading failed.'
    }
} finally { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
$p = Start-Process -FilePath $exe -ArgumentList @('--taskbar', '--stats', '--skin=all') -WorkingDirectory $repo -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 5
    if ($p.HasExited) { throw 'Portable taskbar process exited unexpectedly.' }
    $stats = Get-Content -LiteralPath (Join-Path $stage.FullName 'falcomdeskling_stats.txt') -Encoding Unicode
    $samples = @($stats | Where-Object { $_ -match '^t=' })
    if ($samples.Count -lt 2 -or ($samples | Where-Object { $_ -notmatch ' n=1 ' })) { throw 'Portable taskbar did not produce single-entity samples.' }
} finally { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
if ((Test-Path -LiteralPath (Join-Path $stage.FullName 'Enemy1')) -and (Test-Path -LiteralPath (Join-Path $stage.FullName 'Enemy2'))) {
    $p = Start-Process -FilePath $exe -ArgumentList @('--taskbar', '--stats', '--enemy1=1', '--enemy2=1', '--enemyauto=2') -WorkingDirectory $repo -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Seconds 6
        if ($p.HasExited) { throw 'Portable battle process exited unexpectedly.' }
        $battle = Get-Content -LiteralPath (Join-Path $stage.FullName 'falcomdeskling_stats.txt') -Encoding Unicode
        foreach ($camp in @('Enemy1', 'Enemy2')) {
            if (-not ($battle | Where-Object { $_ -match "faction=$camp " })) { throw "Portable package failed to spawn $camp" }
        }
        Write-Output 'PASS: portable three-camp taskbar spawning.'
    } finally { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
}
Write-Output "Portable ZIP passed: $count checksums, no reparse points, config loaded, taskbar ran from a path with spaces."
Write-Output "Extracted test copy: $($stage.FullName)"

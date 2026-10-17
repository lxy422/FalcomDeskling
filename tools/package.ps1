# Produces a portable ZIP containing real files, never development junctions.
[CmdletBinding()]
param(
    [string]$ChrRoot,
    [string]$EnemyRoot,
    [string]$BuildRoot = 'build',
    [switch]$IncludeEnemies, # retained compatibility; enemies now included by default
    [switch]$OmitEnemies,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildRoot)) { $BuildRoot } else { Join-Path $repo $BuildRoot }
if (-not $SkipBuild) {
    & (Join-Path $repo 'build.cmd') release $build
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
$bin = Join-Path $build 'bin'
if (-not $ChrRoot) { $ChrRoot = Join-Path $bin 'chr' }
if (-not $EnemyRoot) { $EnemyRoot = $bin }
if (-not (Test-Path -LiteralPath (Join-Path $bin 'FalcomDeskling.exe'))) { throw 'Executable missing. Run build.cmd first.' }
if (-not (Test-Path -LiteralPath $ChrRoot -PathType Container)) { throw 'Character art missing. Supply -ChrRoot <path>.' }
if (-not $OmitEnemies) {
    foreach ($camp in @('Enemy1', 'Enemy2')) {
        if (-not (Test-Path -LiteralPath (Join-Path $EnemyRoot $camp) -PathType Container)) { throw "$camp missing. Supply -EnemyRoot <parent containing Enemy1 and Enemy2>, or -OmitEnemies." }
    }
}

# Each invocation gets a new output directory; never overwrite or delete an
# existing release. Copy individual files to materialize any source junctions.
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$name = 'FalcomDeskling-' + $stamp + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6)
$dist = Join-Path $repo 'dist'
$stage = Join-Path $dist $name
New-Item -ItemType Directory -Path $stage -Force | Out-Null
function Copy-Art([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($entry in Get-ChildItem -LiteralPath $Source -Force) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Nested junction/symlink in art: $($entry.FullName). Supply a plain resource tree."
        }
        $target = Join-Path $Destination $entry.Name
        if ($entry.PSIsContainer) { Copy-Art $entry.FullName $target }
        else { Copy-Item -LiteralPath $entry.FullName -Destination $target }
    }
}
Copy-Item -LiteralPath (Join-Path $bin 'FalcomDeskling.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repo 'resource\falcom.ico') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination $stage
Copy-Art (Join-Path $repo 'docs') (Join-Path $stage 'docs')
Copy-Art $ChrRoot (Join-Path $stage 'chr')
if (-not $OmitEnemies) { foreach ($camp in @('Enemy1', 'Enemy2')) { Copy-Art (Join-Path $EnemyRoot $camp) (Join-Path $stage $camp) } }

# ASCII launchers also work in paths containing spaces or Chinese characters.
@('@echo off', 'cd /d "%~dp0"', 'start "" "%~dp0FalcomDeskling.exe" --taskbar') |
    Set-Content -LiteralPath (Join-Path $stage 'Taskbar.cmd') -Encoding ASCII
@('@echo off', 'cd /d "%~dp0"', 'start "" "%~dp0FalcomDeskling.exe"') |
    Set-Content -LiteralPath (Join-Path $stage 'Desktop.cmd') -Encoding ASCII
$manifest = foreach ($f in Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash, $f.FullName.Substring($stage.Length + 1)
}
$manifest | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding UTF8
$zip = Join-Path $dist ($name + '.zip')
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
Write-Output $zip

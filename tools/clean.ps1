# Remove generated artifacts, preserving the current executable and all runtime art.
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$build = Join-Path $repo 'build'
$targets = @('dist', 'build/obj', 'build/emote-fix', 'build/startup-fix', 'build_log.txt')
if (Test-Path -LiteralPath $build) {
    $targets += @(Get-ChildItem -LiteralPath $build -Force | Where-Object {
        $_.Name -like 'scan-fixture-*' -or $_.Name -like 'portable smoke *' -or
        $_.Name -like 'battle-live-*.txt'
    } | ForEach-Object { 'build/' + $_.Name })
}
foreach ($name in @('1.bat', '1 - 副本.bat', 'regression.exe',
    'falcomdeskling_config.txt', 'falcomdeskling_dump.txt', 'falcomdeskling_error.txt',
    'falcomdeskling_stats.txt', 'taskbar_bottom.png', 'taskbar_shot.png',
    'taskbar_zoom.png', 'tb_full.png', 'tb_zoom.png')) {
    $targets += 'build/bin/' + $name
}
$bin = Join-Path $build 'bin'
if (Test-Path -LiteralPath $bin) {
    $targets += @(Get-ChildItem -LiteralPath $bin -File -Filter 'enemy-split-*.json' |
        ForEach-Object { 'build/bin/' + $_.Name })
}
function Remove-GeneratedItem([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith($repo + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "Outside workspace: $absolute" }
    $item = Get-Item -LiteralPath $absolute -Force
    # Never follow directory links into external resource trees.
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        if ($item.PSIsContainer) { [IO.Directory]::Delete($absolute) }
        else { [IO.File]::Delete($absolute) }
    } elseif ($item.PSIsContainer) {
        foreach ($child in @(Get-ChildItem -LiteralPath $absolute -Force)) {
            Remove-GeneratedItem $child.FullName
        }
        Remove-Item -LiteralPath $absolute -Force
    } else {
        Remove-Item -LiteralPath $absolute -Force
    }
}
foreach ($relative in $targets) {
    $target = Join-Path $repo $relative
    if (Test-Path -LiteralPath $target) {
        Remove-GeneratedItem $target
        Write-Output "Removed: $relative"
    }
}
Write-Output 'Cleanup complete. Source, tests, documentation, runtime executable and art preserved.'

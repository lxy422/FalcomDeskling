# Move a legacy Enemy tree into two camps. All moves stay inside the bin folder.
param([string]$BinRoot)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $BinRoot) { $BinRoot = Join-Path $repo 'build\bin' }
$bin = (Resolve-Path -LiteralPath $BinRoot).Path.TrimEnd('\')
$legacy = Join-Path $bin 'Enemy'
$moves = @()
if (Test-Path -LiteralPath $legacy -PathType Container) {
    if ((Get-Item -LiteralPath $legacy).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Legacy Enemy is a junction. Copy its actual resources into bin before splitting.'
    }
    foreach ($skin in Get-ChildItem -LiteralPath $legacy -Directory) {
        if ($skin.Name -notmatch '^CH(\d+)$') { throw "Unrecognized enemy folder: $($skin.Name)" }
        $camp = if ([int]$matches[1] -lt 10000) { 'Enemy1' } else { 'Enemy2' }
        $source = [IO.Path]::GetFullPath($skin.FullName)
        $destination = [IO.Path]::GetFullPath((Join-Path (Join-Path $bin $camp) $skin.Name))
        foreach ($path in @($source, $destination)) {
            if (-not $path.StartsWith($bin + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Move escapes bin directory.' }
        }
        if (Test-Path -LiteralPath $destination) { throw "Destination already exists: $destination" }
        $moves += [pscustomobject]@{ Source = $source; Destination = $destination }
    }
}
foreach ($camp in @('Enemy1', 'Enemy2')) { New-Item -ItemType Directory -Path (Join-Path $bin $camp) -Force | Out-Null }
if ($moves.Count) {
    $ledger = Join-Path $bin ('enemy-split-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
    $moves | ConvertTo-Json | Set-Content -LiteralPath $ledger -Encoding UTF8
    foreach ($move in $moves) { Move-Item -LiteralPath $move.Source -Destination $move.Destination }
    Write-Output "Saved move ledger: $ledger"
}
foreach ($camp in @('Enemy1', 'Enemy2')) {
    $shared = Join-Path $legacy 'monster.xml'
    $campConfig = Join-Path (Join-Path $bin $camp) 'monster.xml'
    if ((Test-Path -LiteralPath $shared -PathType Leaf) -and -not (Test-Path -LiteralPath $campConfig)) {
        Copy-Item -LiteralPath $shared -Destination $campConfig
    }
    $count = @(Get-ChildItem -LiteralPath (Join-Path $bin $camp) -Directory).Count
    Write-Output "$camp : $count skins"
}
if ((Test-Path -LiteralPath $legacy -PathType Container) -and @(Get-ChildItem -LiteralPath $legacy -Force).Count -eq 0) {
    # Nonrecursive deletion of the now-empty, already validated legacy folder.
    [IO.Directory]::Delete($legacy, $false)
}

# Check the actual executable and its adjacent resources from different CWDs.
param([string]$Exe)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $repo 'build\bin\FalcomDeskling.exe' }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$bin = Split-Path -Parent $Exe
$config = Join-Path $bin 'falcomdeskling_config.txt'
$errorLog = Join-Path $bin 'falcomdeskling_error.txt'
Write-Output "Executable: $Exe"
foreach ($cwd in @($repo, $bin, [IO.Path]::GetTempPath()) | Select-Object -Unique) {
    $p = Start-Process -FilePath $Exe -ArgumentList @('--dumpconfig', '--skin=all') -WorkingDirectory $cwd -WindowStyle Hidden -PassThru
    try {
        if (-not $p.WaitForExit(10000)) {
            $detail = if (Test-Path -LiteralPath $errorLog) { (Get-Content -LiteralPath $errorLog -Encoding Unicode | Select-Object -Last 8) -join "`n" } else { 'No error log.' }
            throw "Startup timed out from $cwd`n$detail"
        }
        if ($p.ExitCode -ne 0) { throw "Startup failed: exit $($p.ExitCode), CWD $cwd" }
        $count = @(Get-Content -LiteralPath $config -Encoding Unicode | Where-Object { $_ -eq '---- character config ----' }).Count
        if ($count -eq 0) { throw "No characters dumped from CWD $cwd" }
        Write-Output "PASS: $count characters loaded; CWD=$cwd"
    } finally {
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    }
}
for ($trial = 0; $trial -lt 30; $trial++) {
    $p = Start-Process -FilePath $Exe -ArgumentList '--dumpconfig' -WorkingDirectory $repo -WindowStyle Hidden -PassThru
    try {
        if (-not $p.WaitForExit(5000) -or $p.ExitCode -ne 0) { throw "Random startup failed on trial $trial" }
        $count = @(Get-Content -LiteralPath $config -Encoding Unicode | Where-Object { $_ -eq '---- character config ----' }).Count
        if ($count -ne 1) { throw 'Random startup did not select exactly one character.' }
    } finally { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
}
Write-Output 'PASS: 30 random startup selections.'

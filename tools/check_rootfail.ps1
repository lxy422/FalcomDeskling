# Prove that a bad --root fails loudly and says which path it looked at,
# instead of quietly falling back to some absolute path on this machine.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param([int]$Seconds = 2)

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$errlog = Join-Path $root "build\bin\falcomdeskling_error.txt"

if (Test-Path $errlog) { Remove-Item $errlog -Force }

$p = Start-Process -FilePath $exe -ArgumentList @("--root=Z:\nope") -PassThru
Start-Sleep -Seconds $Seconds
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

if (Test-Path $errlog) {
    Write-Host "---- error log (what the program said) ----"
    Get-Content $errlog -Encoding Unicode
} else {
    Write-Host "[X] no error log written - the failure was silent"
}

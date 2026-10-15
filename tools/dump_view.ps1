# 看 falcomdeskling_dump.txt：那是 UTF-16，直接 Get-Content 会当乱码。
# 默认只挑每个动作集 dir0 那一行，武器那两套也一并带出来
param(
    [string]$Path = "build\bin\falcomdeskling_dump.txt",
    [string]$Filter = "dir0",
    [switch]$All
)

$lines = Get-Content $Path -Encoding Unicode
foreach ($l in $lines) {
    if ($All) { Write-Host $l; continue }
    if ($l -like "*$Filter*") { Write-Host $l }
}

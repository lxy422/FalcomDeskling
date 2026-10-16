# Does --dumpconfig read the real character.xml files?
#
# Two runs, same people:
#   1. --dumpconfig        xml on top of the constants
#   2. --dumpconfig --no-config   constants only
# The diff between the two is the proof that the xml path is actually doing
# something. If they come out identical, the files were never found and the
# whole feature is silently off.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [string]$Skins = "all",
    [int]$Keep = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$out = Join-Path $root "build\bin\falcomdeskling_config.txt"

function Get-Dump($extra) {
    if (Test-Path $out) { Remove-Item $out -Force }
    $args = @("--skin=$Skins", "--dumpconfig")
    if ($extra) { $args += ($extra -split ' ') }
    Start-Process -FilePath $exe -ArgumentList $args -Wait | Out-Null
    if (-not (Test-Path $out)) { return $null }
    return (Get-Content $out -Encoding Unicode)
}

$xml = Get-Dump ""
if ($null -eq $xml) { Write-Host "NO CONFIG DUMP"; exit 1 }
$const = Get-Dump "--no-config"
if ($null -eq $const) { Write-Host "NO CONFIG DUMP (control)"; exit 1 }

# How many people did it report?
$people = ($xml | Select-String -Pattern '^---- character config ----').Count
Write-Host ("people reported: {0}" -f $people)

# Fields that came from xml rather than the constants
$xmlLayer = ($xml | Select-String -Pattern '\[xml\]').Count
$constLayer = ($xml | Select-String -Pattern '\[常量\]').Count
Write-Host ("fields from xml: {0}   fields left at constant: {1}" -f $xmlLayer, $constLayer)

# The control must be strictly "all constants"
$constXml = ($const | Select-String -Pattern '\[xml\]').Count
Write-Host ("control: fields marked [xml] = {0}  (must be 0)" -f $constXml)

# A field present in the xml run but not in the control proves the file was read
$diff = Compare-Object -ReferenceObject $const -DifferenceObject $xml
$changed = ($diff | Where-Object { $_.SideIndicator -eq '=>' } |
            Select-String -Pattern '=' |
            Where-Object { $_.ToString() -notmatch '---- character' } |
            Measure-Object).Count
Write-Host ("lines that differ because of xml: {0}" -f $changed)

# Warnings are the other half of the story: a typo'd attribute name shows up here
$warn = ($xml | Select-String -Pattern '^\s+! ').Count
Write-Host ("parse warnings across all people: {0}" -f $warn)
if ($warn -gt 0) {
    $xml | Select-String -Pattern '^\s+! ' | Select-Object -First 6 | ForEach-Object {
        Write-Host ("   " + $_.Line.Trim())
    }
}

# Encounter rows live on lines indented six spaces. Matching "id=0" loosely
# also catches the header line "  id=0  阵营=..." and reports a phantom.
$wild = ($xml | Select-String -Pattern '^      \* ').Count
$idZero = ($xml | Select-String -Pattern '^      id=0 ').Count
Write-Host ("wildcard rows: {0}   rows that fell back to id=0: {1}  (want 0)" -f $wild, $idZero)

# The monster-class baseline row: WpfTest writes id="100" for "any enemy".
# Under our ranges 100 is not a monster id, so it must be translated into the
# whole class rather than dropped.
$base = ($xml | Select-String -Pattern 'far=chase\s+near=attack\s+\+\S+').Count
Write-Host ("'see an enemy' baseline rows: {0}  (want >= {1})" -f $base, $people)

if ($Keep -eq 0) { Remove-Item $out -Force -ErrorAction SilentlyContinue }

$bad = 0
if ($constXml -ne 0) { Write-Host "FAIL: control still reads xml"; $bad++ }
if ($idZero -ne 0)   { Write-Host "FAIL: wildcard row parsed as id=0"; $bad++ }
if ($base -lt $people) { Write-Host "FAIL: monster baseline lost on some people"; $bad++ }
if ($bad -eq 0) { Write-Host "`nall ok" } else { exit 1 }

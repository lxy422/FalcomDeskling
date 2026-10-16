# The claim this whole step hangs on: "no file = today's behaviour".
#
# So don't eyeball it. Run --dumpconfig --no-config, pull every effective value
# out, and compare each one against the constant it is supposed to fall back to.
# The constants are read out of config.h rather than hardcoded here, so the test
# follows the code instead of rotting next to it.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$cfg = Join-Path $root "src\config.h"
$out = Join-Path $root "build\bin\falcomdeskling_config.txt"

# 1. constants straight out of config.h
$src = Get-Content $cfg -Encoding UTF8
function ConstOf($name) {
    foreach ($l in $src) {
        if ($l -match "constexpr\s+\w+\s+$name\s*=\s*([-\d.]+)") { return [double]$matches[1] }
    }
    throw "constant $name not found in config.h"
}

# dump field -> config.h constant. Anything not listed here is not checked,
# so adding a new field will not silently pass.
$MAP = [ordered]@{
    walkPx       = 'WALK_PX'
    runPx        = 'RUN_PX'
    armedRunPx   = 'ARMED_RUN_PX'
    bodyLen      = 'SPRITE_BODY_HEIGHT'
    nearLen      = 'RUN_START_LEN'
    farLen       = 'RUN_END_LEN'
    keepLen      = 'KEEP_LEN'
    encNearLen   = 'ENC_NEAR_LEN'
    encFarLen    = 'ENC_FAR_LEN'
    attackLen    = 'ATTACK_LEN'
    wanderSec    = 'WANDER_SEC'
    emoteGapSec  = 'EMOTE_GAP_SEC'
    emoteShowSec = 'EMOTE_SHOW_SEC'
    idleSec      = 'IDLE_SEC'
    disarmSec    = 'DISARM_SEC'
}

# 2. dump with every file switched off
Stop-Process -Name FalcomDeskling -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
if (Test-Path $out) { Remove-Item $out -Force }
Start-Process -FilePath $exe -ArgumentList @('--skin=all', '--dumpconfig', '--no-config') -Wait
if (-not (Test-Path $out)) { Write-Host "NO CONFIG DUMP"; exit 1 }
$lines = Get-Content $out -Encoding Unicode

# 3. every value that appears must equal its constant
$bad = 0
$seen = @{}
foreach ($l in $lines) {
    if ($l -notmatch '^\s{2}(\w+)\s+=\s+([-\d.]+)\s*\[') { continue }
    $key = $matches[1]
    $val = [double]$matches[2]
    if (-not $MAP.Contains($key)) { continue }
    $seen[$key] = 1
    $want = ConstOf $MAP[$key]
    if ([math]::Abs($val - $want) -gt 0.001) {
        Write-Host ("  MISMATCH {0}: dump={1} config.h={2}" -f $key, $val, $want)
        $bad++
    }
}

# 4. a field that never showed up means the dump stopped covering it
foreach ($k in $MAP.Keys) {
    if (-not $seen.ContainsKey($k)) {
        Write-Host ("  MISSING: {0} never appeared in the dump" -f $k)
        $bad++
    }
}

# 5. and nothing may claim to come from xml in this mode
$xmlLayer = ($lines | Select-String -Pattern '\[xml\]').Count
Write-Host ("fields checked: {0}/{1}   mismatches: {2}   marked [xml]: {3}" `
            -f $seen.Count, $MAP.Count, $bad, $xmlLayer)
if ($xmlLayer -ne 0) { Write-Host "FAIL: --no-config still read xml"; $bad++ }

Remove-Item $out -Force -ErrorAction SilentlyContinue
if ($bad -eq 0) { Write-Host "all ok" } else { exit 1 }

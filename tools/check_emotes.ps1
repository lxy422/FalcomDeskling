# Sweep every emote id and report, per id, whether a bubble actually appeared
# and where it landed relative to the owner's head line.
#
# headGap is by design 0.16 * (distance from feet to head line): the tail is
# supposed to overlap the hair a little. So the check is not "is headGap small"
# but "is headGap / ExtUp close to 0.16".
#
# Everything must come from ONE sample block. Reading "bubbling" as a max over
# the whole file while taking the rect from the last line makes an expired
# bubble look like "busy but no window" - that is a bug in this script.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [int]$Seconds = 4,
    [int]$Last = 18,
    [string]$Skin = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"

# Parse one block: returns $null when nothing was bubbling in it.
function Read-Block($block) {
    $bub = 0
    $posY = $null; $head = $null; $gap = $null; $size = $null
    foreach ($l in $block) {
        if ($l -match 'bubbling=(\d+)') { $bub = [int]$matches[1] }
        if ($l -match 'pos=-?[\d.]+,(-?[\d.]+)') { $posY = [double]$matches[1] }
        if ($l -match ' head=(-?[\d.]+)') { $head = [double]$matches[1] }
        if ($l -match 'bubble=\([-\d,]+\)-\([-\d,]+\) (\d+)x(\d+) headGap=(-?\d+)') {
            $size = ($matches[1] + "x" + $matches[2])
            $gap = [int]$matches[3]
        }
    }
    if ($bub -eq 0) { return $null }
    if ($null -eq $size) { return $null }
    $extUp = 0
    if ($null -ne $posY -and $null -ne $head) { $extUp = [int][Math]::Round($posY - $head) }
    return [pscustomobject]@{ Bub = $bub; Size = $size; Gap = $gap; ExtUp = $extUp }
}

for ($id = 1; $id -le $Last; $id++) {
    $lines = $null
    # Two attempts: on a loaded box the process sometimes does not get its
    # first stats flush out within the window, and that shows up as "NO STATS"
    # for an emote that is actually fine. The set of ids that fail this way
    # changes from run to run, which is what told us it is the harness.
    for ($try = 1; $try -le 2 -and $null -eq $lines; $try++) {
        # Make sure nothing is still writing before we delete the file. Without
        # this, a block from the previous id gets read as if it belonged to this
        # one - which is what made one row show a 128x256 bubble that no frame
        # table produces.
        Stop-Process -Name FalcomDeskling -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 300
        if (Test-Path $stats) { Remove-Item $stats -Force }

        # No skin name on purpose: every skin in chr\ is Chinese, and Chinese
        # in a .ps1 breaks the parser here. Leaving --skin out makes the exe
        # pick one.
        $args = @("--stats", "--tick=33", "--emote=$id")
        if ($Skin -ne "") { $args += "--skin=$Skin" }
        $p = Start-Process -FilePath $exe -ArgumentList $args -PassThru
        # Wait for the first stats line instead of sleeping blindly.
        for ($i = 0; $i -lt 30 -and -not (Test-Path $stats); $i++) {
            Start-Sleep -Milliseconds 200
        }
        Start-Sleep -Seconds $Seconds
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500

        if (Test-Path $stats) { $lines = Get-Content $stats -Encoding Unicode }
    }

    if ($null -eq $lines) {
        Write-Host ("emote={0,-3} NO STATS (twice)" -f $id)
        continue
    }

    # Split into blocks on the t= lines, keep the last one that had a bubble.
    $best = $null
    $block = @()
    foreach ($l in $lines) {
        if ($l -match '^t=') {
            $r = Read-Block $block
            if ($r) { $best = $r }
            $block = @($l)
        } else {
            $block += $l
        }
    }
    $r = Read-Block $block
    if ($r) { $best = $r }

    if ($null -eq $best) {
        Write-Host ("emote={0,-3} NO BUBBLE IN ANY BLOCK" -f $id)
        continue
    }
    $verdict = "ok"
    if ($best.Size -eq "0x0") {
        $verdict = "ZERO-SIZED WINDOW"
    } elseif ($best.ExtUp -gt 0) {
        $ratio = $best.Gap / [double]$best.ExtUp
        if ([Math]::Abs($ratio - 0.16) -gt 0.05) {
            $verdict = ("BAD SINK ratio={0:F3} (want 0.16)" -f $ratio)
        } else {
            $verdict = ("ok ratio={0:F3}" -f $ratio)
        }
    } else {
        $verdict = "NO EXTUP"
    }
    Write-Host ("emote={0,-3} size={1,-8} headGap={2,-5} extUp={3,-5} {4}" -f `
        $id, $best.Size, $best.Gap, $best.ExtUp, $verdict)
}

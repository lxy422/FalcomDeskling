# Turn resource\emotio.png into a C++ byte array so the atlas is compiled into
# the exe instead of being read from disk.
#
# This exists because rc.exe is not available on this box (no Windows SDK bin
# folder), so the normal RCDATA route is closed. 23KB of PNG becomes ~140KB of
# hex text, which is a fine price for "no external file next to the exe".
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param(
    [string]$Source = "resource\emotio.png",
    [string]$Dest = "src\emotio_data.h"
)

$bytes = [System.IO.File]::ReadAllBytes($Source)
$fi = Get-Item $Source

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("// Generated from $(Split-Path -Leaf $Source) ($($fi.Length) bytes). DO NOT EDIT.")
[void]$sb.AppendLine("// Regenerate with:  powershell -File tools\embed_emotio.ps1")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("// The emote atlas, PNG-encoded, compiled straight into the exe.")
[void]$sb.AppendLine("static const unsigned char kEmotioPng[] = {")

for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($i % 20 -eq 0) { [void]$sb.Append("    ") }
    [void]$sb.Append(("0x{0:x2}," -f $bytes[$i]))
    if (($i % 20) -eq 19) { [void]$sb.AppendLine() } else { [void]$sb.Append(" ") }
}
if ($bytes.Length % 20 -ne 0) { [void]$sb.AppendLine() }

[void]$sb.AppendLine("};")
[void]$sb.AppendLine("static const unsigned int kEmotioPngLen = " + $bytes.Length + "u;")

[System.IO.File]::WriteAllText($Dest, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Output ("embedded " + $bytes.Length + " bytes -> " + $Dest)

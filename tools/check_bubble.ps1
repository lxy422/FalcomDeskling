# Bubble geometry check.
#
# The bubble is its own layered window (plan option B), so the way to tell
# whether it is drawn where it should be is to compare the two rects: the
# owner's window and the bubble window belonging to the same entity.
#
# A bubble is correct when its bottom edge sits slightly ABOVE the owner's head
# line (the tail overlaps the hair a bit) and it is horizontally near the
# owner's centre. If the bubble's bottom is below the owner's bottom, the tail
# is pointing at their feet.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage, so Chinese in
# this file breaks the parser. Pass the skin name on the command line instead.
param(
    [int]$Seconds = 6,
    [string]$Skin = "",
    [int]$Emote = 13
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\bin\FalcomDeskling.exe"
$stats = Join-Path $root "build\bin\falcomdeskling_stats.txt"

# The app is single-instance, so a leftover process from an earlier run makes
# this spawn exit immediately and the stats file never appears.
Stop-Process -Name FalcomDeskling -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
if (Test-Path $stats) { Remove-Item $stats -Force }

# csc validates every entry in LIB and hard-fails if one of them does not
# exist. This box has a leftover "C:\Program Files (x86)\Microsoft Visual
# Studio\VC98\mfc\lib" in there, which kills Add-Type before it even compiles.
# Drop the dead entries and keep the rest.
if ($env:LIB) {
    $keep = ($env:LIB -split ';' | Where-Object { $_ -ne '' -and (Test-Path $_) }) -join ';'
    $env:LIB = $keep
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class Win {
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lp, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static List<int[]> Visible(int pid) {
        var outp = new List<int[]>();
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            RECT r;
            if (GetWindowRect(h, out r) && IsWindowVisible(h)) {
                int w = r.Right - r.Left, ht = r.Bottom - r.Top;
                if (w > 0 && ht > 0 && GetWindowTextLength(h) == 0) {
                    outp.Add(new int[] { h.ToInt64().GetHashCode(), r.Left, r.Top, w, ht });
                }
            }
            return true;
        }, IntPtr.Zero);
        return outp;
    }
}
"@

$args = @("--stats", "--tick=33", "--emote=$Emote")
if ($Skin -ne "") { $args += "--skin=$Skin" }

$p = Start-Process -FilePath $exe -ArgumentList $args -PassThru
Start-Sleep -Seconds 1

$wins = [Win]::Visible($p.Id)
Write-Host ("visible untitled windows at t=1: " + $wins.Count)
foreach ($x in $wins) {
    Write-Host ("  " + $x[3] + "x" + $x[4] + " at " + $x[1] + "," + $x[2])
}

Start-Sleep -Seconds ($Seconds - 1)
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

Write-Host "---- stats ----"
Get-Content $stats -Encoding Unicode | ForEach-Object { Write-Host $_ }

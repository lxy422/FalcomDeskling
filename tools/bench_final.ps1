param([int]$Span = 12)
$exe = 'c:\Users\lxy_42\source\repos\FalcomDeskling\build\bin\FalcomDeskling.exe'
$bin = Split-Path $exe
$cases = @(
    @{ n = 'ulw @33ms(default)'; a = @() },
    @{ n = 'ulw @15ms';          a = @('--tick=15') },
    @{ n = 'move @33ms';         a = @('--move') },
    @{ n = 'move @15ms';         a = @('--move', '--tick=15') },
    @{ n = 'nopresent (floor)';  a = @('--nopresent') }
)
function Parse($line) {
    $h = @{}
    foreach ($kv in ($line.Split(' ') | Where-Object { $_ -match '=' })) {
        $p = $kv.Split('=', 2)
        $h[$p[0]] = ($p[1] -replace '[^0-9.\-]', '')
    }
    return $h
}
foreach ($c in $cases) {
    $a = @('--stats') + $c.a
    Remove-Item "$bin\falcomdeskling_stats.txt" -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $exe -ArgumentList $a -WorkingDirectory $bin -PassThru
    Start-Sleep -Seconds 3
    $p.Refresh(); $c1 = $p.TotalProcessorTime.TotalSeconds
    Start-Sleep -Seconds $Span
    $p.Refresh(); $c2 = $p.TotalProcessorTime.TotalSeconds
    $ws = [math]::Round($p.WorkingSet64 / 1MB, 1)
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }

    $lines = [System.IO.File]::ReadAllLines("$bin\falcomdeskling_stats.txt", [System.Text.Encoding]::Unicode) |
             Where-Object { $_ -match 'ticks=' }
    $f = Parse $lines[0]
    $l = Parse $lines[-1]
    $dt = [double]$l['t'] - [double]$f['t']
    if ($dt -le 0) { $dt = [double]$Span }

    $cpu = [math]::Round(($c2 - $c1) / $Span * 100, 2)
    $fps = [math]::Round(([double]$l['ticks'] - [double]$f['ticks']) / $dt, 1)
    $ups = [math]::Round(([double]$l['up'] - [double]$f['up']) / $dt, 1)
    $mvs = [math]::Round(([double]$l['mv'] - [double]$f['mv']) / $dt, 1)
    $nUp = [double]$l['up'] - [double]$f['up']
    $nMv = [double]$l['mv'] - [double]$f['mv']
    $perUp = if ($nUp -gt 0) { [math]::Round(([double]$l['upload'] - [double]$f['upload']) / $nUp, 2) } else { 0 }
    $perMv = if ($nMv -gt 0) { [math]::Round(([double]$l['move'] - [double]$f['move']) / $nMv, 2) } else { 0 }

    $line = $c.n.PadRight(22) + ' cpu=' + ('{0,6}' -f $cpu) + '% fps=' + ('{0,5}' -f $fps) +
            ' up/s=' + ('{0,5}' -f $ups) + ' mv/s=' + ('{0,5}' -f $mvs) +
            ' ms/up=' + ('{0,6}' -f $perUp) + ' ms/mv=' + ('{0,6}' -f $perMv) +
            ' ws=' + $ws + 'MB'
    Write-Output $line
    Start-Sleep -Seconds 1
}

param([int[]]$Scales = @(50, 100, 200), [int]$Warm = 4, [int]$Span = 6)
$exe = 'c:\Users\lxy_42\source\repos\FalcomDeskling\build\bin\FalcomDeskling.exe'
foreach ($s in $Scales) {
    $p = Start-Process -FilePath $exe -ArgumentList "--scale=$s" -WorkingDirectory (Split-Path $exe) -PassThru
    Start-Sleep -Seconds $Warm
    $p.Refresh(); $c1 = $p.TotalProcessorTime.TotalSeconds
    Start-Sleep -Seconds $Span
    $p.Refresh(); $c2 = $p.TotalProcessorTime.TotalSeconds
    $alive = -not $p.HasExited
    Write-Output ("scale={0} alive={1} cpu={2}% ws={3}MB" -f $s, $alive, [math]::Round(($c2-$c1)/$Span*100,2), [math]::Round($p.WorkingSet64/1MB,1))
    if ($alive) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 1
}

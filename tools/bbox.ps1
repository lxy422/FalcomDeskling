param([string]$Path = 'C:\Users\lxy_42\source\repos\WpfTest\Resource\Chr\科洛丝\2.Walk')
Add-Type -AssemblyName System.Drawing
$cell = 256
for ($d = 0; $d -lt 8; $d++) {
    $f = Join-Path $Path ("dir{0}.png" -f $d)
    if (-not (Test-Path $f)) { continue }
    $bmp = New-Object System.Drawing.Bitmap($f)
    $n = [math]::Floor($bmp.Width / $cell)
    $minX = 99999; $minY = 99999; $maxX = -1; $maxY = -1
    for ($x = 0; $x -lt $bmp.Width; $x++) {
        for ($y = 0; $y -lt $bmp.Height; $y++) {
            if ($bmp.GetPixel($x, $y).A -ne 0) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    $bmp.Dispose()
    Write-Output ("dir{0} frames={1} bbox=({2},{3})-({4},{5}) size={6}x{7} fill={8:P1}" -f `
        $d, $n, $minX, $minY, $maxX, $maxY, ($maxX - $minX + 1), ($maxY - $minY + 1), `
        ((($maxX - $minX + 1) * ($maxY - $minY + 1)) / [double](256 * 256)))
}

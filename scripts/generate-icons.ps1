# Generates the placeholder tray icons in src/Outatime/icons.
# Each .ico holds PNG frames for the sizes Windows asks for at common DPI scales.
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$outDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'src/Outatime/icons'
New-Item -ItemType Directory -Force $outDir | Out-Null

function New-Frame([int]$size, [System.Drawing.Color]$background) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::Transparent)

    # Rounded square
    $radius = [Math]::Max(2, $size * 0.22)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $max = $size - 1
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($max - $d, 0, $d, $d, 270, 90)
    $path.AddArc($max - $d, $max - $d, $d, $d, 0, 90)
    $path.AddArc(0, $max - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $g.FillPath((New-Object System.Drawing.SolidBrush $background), $path)

    # "A" glyph
    $font = New-Object System.Drawing.Font 'Segoe UI Semibold', ($size * 0.62), ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = 'Center'
    $format.LineAlignment = 'Center'
    $rect = New-Object System.Drawing.RectangleF 0, ($size * 0.04), $size, $size
    $g.DrawString('A', $font, [System.Drawing.Brushes]::White, $rect, $format)

    $g.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    return , $stream.ToArray()
}

function Write-Icon([string]$path, [System.Drawing.Color]$background) {
    $frames = foreach ($size in $sizes) { , (New-Frame $size $background) }
    $out = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $out
    # ICONDIR
    $w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $s = $sizes[$i]
        # ICONDIRENTRY; a size of 256 is stored as 0
        $w.Write([byte]($s % 256)); $w.Write([byte]($s % 256)); $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([UInt16]1); $w.Write([UInt16]32)
        $w.Write([UInt32]$frames[$i].Length); $w.Write([UInt32]$offset)
        $offset += $frames[$i].Length
    }
    foreach ($frame in $frames) { $w.Write($frame) }
    $w.Flush()
    [System.IO.File]::WriteAllBytes($path, $out.ToArray())
    Write-Host "Wrote $path"
}

Write-Icon (Join-Path $outDir 'outatime.ico') ([System.Drawing.Color]::FromArgb(0, 103, 192))
Write-Icon (Join-Path $outDir 'outatime-paused.ico') ([System.Drawing.Color]::FromArgb(138, 138, 138))

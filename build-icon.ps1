param()
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = $PSScriptRoot
$iconDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $iconDir -Force | Out-Null

# This dependency-free renderer intentionally supports only the rounded rectangles
# used by interval.svg. Unsupported SVG features fail instead of rendering incorrectly.
$readerSettings = New-Object System.Xml.XmlReaderSettings
$readerSettings.DtdProcessing = [System.Xml.DtdProcessing]::Prohibit
$readerSettings.XmlResolver = $null
$svgText = Get-Content -LiteralPath (Join-Path $projectRoot 'assets\interval.svg') -Raw -Encoding UTF8
$stringReader = New-Object System.IO.StringReader $svgText
$reader = [System.Xml.XmlReader]::Create($stringReader,$readerSettings)
$svg = New-Object System.Xml.XmlDocument
$svg.XmlResolver = $null
try { $svg.Load($reader) } finally { $reader.Dispose(); $stringReader.Dispose() }
if ($svg.DocumentElement.LocalName -ne 'svg' -or $svg.DocumentElement.GetAttribute('viewBox') -ne '0 0 64 64') { throw 'Expected the 64 x 64 Interval SVG.' }
function Svg-Number($Node,[string]$Name) {
    return [float]::Parse($Node.GetAttribute($Name),[Globalization.CultureInfo]::InvariantCulture)
}
$frames = New-Object 'System.Collections.Generic.List[byte[]]'
$sizes = @(16,20,24,32,40,48,64,128,256)
foreach ($size in $sizes) {
    $canvas = New-Object Drawing.Bitmap ($size*4),($size*4),([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($canvas)
    $bitmap = New-Object Drawing.Bitmap $size,$size,([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $target = [Drawing.Graphics]::FromImage($bitmap)
    $stream = New-Object System.IO.MemoryStream
    try {
        $graphics.Clear([Drawing.Color]::Transparent)
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.ScaleTransform(($size*4/64.0),($size*4/64.0))
        foreach ($node in $svg.DocumentElement.ChildNodes) {
            if ($node.LocalName -in @('title','desc')) { continue }
            if ($node.LocalName -ne 'rect') { throw 'Unsupported SVG shape.' }
            foreach ($attribute in $node.Attributes) {
                if ($attribute.Name -notin @('x','y','width','height','rx','fill')) { throw 'Unsupported SVG attribute.' }
            }
            $x=Svg-Number $node 'x'; $y=Svg-Number $node 'y'
            $width=Svg-Number $node 'width'; $height=Svg-Number $node 'height'; $radius=Svg-Number $node 'rx'
            if ($width -le 0 -or $height -le 0 -or $radius -le 0 -or $radius*2 -gt [Math]::Min($width,$height)) { throw 'Invalid SVG rectangle.' }
            $path = New-Object Drawing.Drawing2D.GraphicsPath
            $brush = New-Object Drawing.SolidBrush ([Drawing.ColorTranslator]::FromHtml($node.GetAttribute('fill')))
            try {
                $diameter=$radius*2
                $path.AddArc($x,$y,$diameter,$diameter,180,90)
                $path.AddArc($x+$width-$diameter,$y,$diameter,$diameter,270,90)
                $path.AddArc($x+$width-$diameter,$y+$height-$diameter,$diameter,$diameter,0,90)
                $path.AddArc($x,$y+$height-$diameter,$diameter,$diameter,90,90)
                $path.CloseFigure()
                $graphics.FillPath($brush,$path)
            } finally { $brush.Dispose(); $path.Dispose() }
        }
        $target.Clear([Drawing.Color]::Transparent)
        $target.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
        $target.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $target.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $target.DrawImage($canvas,0,0,$size,$size)
        $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
        $frames.Add($stream.ToArray())
        if ($size -eq 256) { $bitmap.Save((Join-Path $iconDir 'interval-icon-preview.png'),[Drawing.Imaging.ImageFormat]::Png) }
    } finally { $stream.Dispose(); $target.Dispose(); $bitmap.Dispose(); $graphics.Dispose(); $canvas.Dispose() }
}
$file = [System.IO.File]::Open((Join-Path $iconDir 'interval.ico'),[System.IO.FileMode]::Create)
$writer = New-Object System.IO.BinaryWriter $file
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
    $offset = 6+16*$sizes.Count
    for ($i=0;$i -lt $sizes.Count;$i++) {
        $dimension=if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$i].Length); $writer.Write([uint32]$offset)
        $offset += $frames[$i].Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
} finally { $writer.Dispose() }
Write-Output 'Interval SVG converted to a multi-resolution Windows icon (16-256 px).'

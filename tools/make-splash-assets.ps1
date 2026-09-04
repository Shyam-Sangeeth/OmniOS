<#
.SYNOPSIS
    Generate the OmniOS Plymouth artwork.

.DESCRIPTION
    Replaces the first-pass Python generator. That one could only fill pixels,
    so the mark was a flat ring with a hard-edged triangle and there was no
    wordmark — it read as a generic media-player glyph rather than a product.

    GDI+ gives gradients, round line caps and real font rendering, which is
    what actually separates a boot splash from clip art. The tradeoff is that
    regeneration now needs Windows; the PNGs are committed, so Linux builds
    just consume them.

    Palette is the launcher's own (OmniOS.md §12) so the splash and the UI it
    hands over to are visibly the same product.

.EXAMPLE
    pwsh tools/make-splash-assets.ps1 iso/airootfs/usr/share/plymouth/themes/omnios
#>
[CmdletBinding()]
param(
    [string]$OutDir = 'iso/airootfs/usr/share/plymouth/themes/omnios'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$OutDir = (Resolve-Path $OutDir).Path

# --- palette (OmniOS.md §12) ------------------------------------------------
$accent    = [System.Drawing.Color]::FromArgb(255, 0x6C, 0x63, 0xFF)   # #6C63FF
$accentAlt = [System.Drawing.Color]::FromArgb(255, 0x3A, 0x8F, 0xFF)   # #3A8FFF
$textMain  = [System.Drawing.Color]::FromArgb(255, 0xF0, 0xF0, 0xF5)   # #F0F0F5
$textDim   = [System.Drawing.Color]::FromArgb(255, 0x88, 0x88, 0xAA)   # #8888AA

function New-Canvas([int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    return @{ Bitmap = $bmp; Graphics = $g }
}

# Pick the most geometric face actually installed rather than assuming one.
function Get-Face([string[]]$Candidates, [float]$Size, [System.Drawing.FontStyle]$Style) {
    foreach ($name in $Candidates) {
        $f = New-Object System.Drawing.Font($name, $Size, $Style, [System.Drawing.GraphicsUnit]::Pixel)
        if ($f.Name -eq $name) { return $f }
        $f.Dispose()
    }
    return New-Object System.Drawing.Font('Segoe UI', $Size, $Style, [System.Drawing.GraphicsUnit]::Pixel)
}

# GDI+ has no letter-spacing, and tracking is most of what makes a wordmark
# look considered, so glyphs are placed individually.
function Draw-Tracked {
    param($G, [string]$Text, $Font, $Brush, [float]$CenterX, [float]$Y, [float]$Tracking)

    $widths = @()
    $total = 0.0
    foreach ($ch in $Text.ToCharArray()) {
        $w = $G.MeasureString([string]$ch, $Font, 0, [System.Drawing.StringFormat]::GenericTypographic).Width
        $widths += $w
        $total += $w + $Tracking
    }
    $total -= $Tracking

    $x = $CenterX - ($total / 2.0)
    for ($i = 0; $i -lt $Text.Length; $i++) {
        $G.DrawString([string]$Text[$i], $Font, $Brush, $x, $Y, [System.Drawing.StringFormat]::GenericTypographic)
        $x += $widths[$i] + $Tracking
    }
}

# --- logo: orbit mark + wordmark + tagline ----------------------------------
$W = 760; $H = 332
$c = New-Canvas $W $H
$g = $c.Graphics

$markCx = $W / 2.0
$markCy = 96.0
$radius = 62.0

# Soft glow, built from widening translucent rings. Cheap, and it stops the
# mark sitting flat on the background.
for ($i = 14; $i -ge 1; $i--) {
    $alpha = [int](5 + $i * 1.1)
    $pen = New-Object System.Drawing.Pen(
        [System.Drawing.Color]::FromArgb($alpha, $accent.R, $accent.G, $accent.B), [float]($i * 2.4))
    $r = $radius + $i * 0.9
    $g.DrawEllipse($pen, [float]($markCx - $r), [float]($markCy - $r), [float]($r * 2), [float]($r * 2))
    $pen.Dispose()
}

$gradRect = New-Object System.Drawing.RectangleF(
    [float]($markCx - $radius - 20), [float]($markCy - $radius - 20),
    [float]($radius * 2 + 40), [float]($radius * 2 + 40))
$grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    $gradRect, $accentAlt, $accent, 55.0)

# Three arcs with gaps: an orbit, not a closed ring. "Many systems, one place"
# — and it reads as a mark rather than a play button in a circle.
$ring = New-Object System.Drawing.Pen($grad, 11.0)
$ring.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$ring.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
foreach ($arc in @(@(-104, 128), @(40, 92), @(148, 62))) {
    $g.DrawArc($ring, [float]($markCx - $radius), [float]($markCy - $radius),
               [float]($radius * 2), [float]($radius * 2), [float]$arc[0], [float]$arc[1])
}
$ring.Dispose()

# Inner arc, thinner and offset, for depth.
$inner = New-Object System.Drawing.Pen($grad, 4.5)
$inner.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$inner.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
$ir = $radius - 20
$g.DrawArc($inner, [float]($markCx - $ir), [float]($markCy - $ir),
           [float]($ir * 2), [float]($ir * 2), 205.0, 190.0)
$inner.Dispose()

# Play glyph, small and rounded — a nod, not the whole identity.
$tri = New-Object System.Drawing.Drawing2D.GraphicsPath
$s = 21.0
$tri.AddPolygon(@(
    (New-Object System.Drawing.PointF([float]($markCx - $s * 0.55), [float]($markCy - $s))),
    (New-Object System.Drawing.PointF([float]($markCx + $s * 0.95), [float]($markCy))),
    (New-Object System.Drawing.PointF([float]($markCx - $s * 0.55), [float]($markCy + $s)))
))
$playPen = New-Object System.Drawing.Pen($grad, 9.0)
$playPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$g.DrawPath($playPen, $tri)
$g.FillPath($grad, $tri)
$playPen.Dispose(); $tri.Dispose()

# Wordmark. Light weight, lightly tracked: restrained rather than shouty.
$wordFont = Get-Face @('Bahnschrift Light', 'Segoe UI Light', 'Segoe UI') 74 ([System.Drawing.FontStyle]::Regular)
$wordBrush = New-Object System.Drawing.SolidBrush($textMain)
Draw-Tracked $g 'OmniOS' $wordFont $wordBrush $markCx 196.0 3.0

# Tagline, wide tracking, dim — the line that says what it is for.
$tagFont = Get-Face @('Bahnschrift', 'Segoe UI', 'Tahoma') 16 ([System.Drawing.FontStyle]::Regular)
$tagBrush = New-Object System.Drawing.SolidBrush($textDim)
Draw-Tracked $g 'PLAY EVERYTHING' $tagFont $tagBrush $markCx 292.0 7.0

$c.Bitmap.Save((Join-Path $OutDir 'logo.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$grad.Dispose(); $wordFont.Dispose(); $tagFont.Dispose(); $wordBrush.Dispose(); $tagBrush.Dispose()
$g.Dispose(); $c.Bitmap.Dispose()

# --- spinner: a sweeping gradient arc ---------------------------------------
$frames = 12
$S = 68
for ($f = 0; $f -lt $frames; $f++) {
    $sc = New-Canvas $S $S
    $sg = $sc.Graphics
    $start = (360.0 / $frames) * $f - 90.0

    # Trailing tail: several arc slices fading out behind the head.
    for ($t = 0; $t -lt 9; $t++) {
        $a = [int](255 * [Math]::Pow((9 - $t) / 9.0, 1.7))
        $col = [System.Drawing.Color]::FromArgb($a, $accent.R, $accent.G, $accent.B)
        $pen = New-Object System.Drawing.Pen($col, 5.0)
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
        $sg.DrawArc($pen, 8.0, 8.0, [float]($S - 16), [float]($S - 16),
                    [float]($start - $t * 13), 11.0)
        $pen.Dispose()
    }

    $sc.Bitmap.Save((Join-Path $OutDir ("progress-{0}.png" -f $f)), [System.Drawing.Imaging.ImageFormat]::Png)
    $sg.Dispose(); $sc.Bitmap.Dispose()
}

Write-Host "wrote logo.png (${W}x${H}) and $frames spinner frames to $OutDir"

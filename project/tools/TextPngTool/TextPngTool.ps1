param(
    [Parameter(Position = 0)]
    [string]$Command = "render",
    [string]$config,
    [string]$out
)

$ErrorActionPreference = "Stop"

function Clamp-Byte {
    param([double]$Value)
    return [Math]::Max(0, [Math]::Min(255, [int][Math]::Round($Value)))
}

function Read-Number {
    param($Value, [double]$Fallback)
    if ($null -eq $Value) {
        return $Fallback
    }
    return [double]$Value
}

function Read-Bool {
    param($Value, [bool]$Fallback)
    if ($null -eq $Value) {
        return $Fallback
    }
    return [bool]$Value
}

function Read-ArrayNumber {
    param($Values, [int]$Index, [double]$Fallback)
    if ($null -eq $Values -or $Values.Count -le $Index) {
        return $Fallback
    }
    return [double]$Values[$Index]
}

function New-ColorFromArray {
    param($Values, [System.Drawing.Color]$Fallback)
    if ($null -eq $Values -or $Values.Count -lt 4) {
        return $Fallback
    }

    $r = Clamp-Byte (([double]$Values[0]) * 255.0)
    $g = Clamp-Byte (([double]$Values[1]) * 255.0)
    $b = Clamp-Byte (([double]$Values[2]) * 255.0)
    $a = Clamp-Byte (([double]$Values[3]) * 255.0)
    return [System.Drawing.Color]::FromArgb($a, $r, $g, $b)
}

function New-StringFormat {
    $format = [System.Drawing.StringFormat]::GenericTypographic.Clone()
    $format.Alignment = [System.Drawing.StringAlignment]::Near
    $format.LineAlignment = [System.Drawing.StringAlignment]::Near
    $format.Trimming = [System.Drawing.StringTrimming]::None
    $flags = [int]$format.FormatFlags
    $flags = $flags -bor [int][System.Drawing.StringFormatFlags]::MeasureTrailingSpaces
    $format.FormatFlags = [System.Drawing.StringFormatFlags]$flags
    return $format
}

function Resolve-FontFamily {
    param(
        [string]$FontPath,
        [System.Drawing.Text.PrivateFontCollection]$PrivateFonts
    )

    if (-not [string]::IsNullOrWhiteSpace($FontPath) -and (Test-Path -LiteralPath $FontPath)) {
        try {
            $resolved = (Resolve-Path -LiteralPath $FontPath).Path
            $PrivateFonts.AddFontFile($resolved)
            if ($PrivateFonts.Families.Count -gt 0) {
                return $PrivateFonts.Families[0]
            }
        }
        catch {
            Write-Warning ("フォント読み込みに失敗しました。既定フォントへ戻します: " + $_.Exception.Message)
        }
    }

    foreach ($name in @("Meiryo UI", "Yu Gothic UI", "Yu Gothic", "Arial")) {
        try {
            return New-Object -TypeName System.Drawing.FontFamily -ArgumentList $name
        }
        catch {
        }
    }

    return [System.Drawing.FontFamily]::GenericSansSerif
}

function New-TextPath {
    param(
        [string]$Text,
        [System.Drawing.FontFamily]$Family,
        [System.Drawing.FontStyle]$Style,
        [single]$FontSize,
        [System.Drawing.RectangleF]$LayoutRect,
        [System.Drawing.StringFormat]$Format
    )

    $path = New-Object -TypeName System.Drawing.Drawing2D.GraphicsPath
    $path.AddString($Text, $Family, [int]$Style, $FontSize, $LayoutRect, $Format)
    return $path
}

function Save-TransparentTextPng {
    param($Cfg, [string]$OutputPath)

    Add-Type -AssemblyName System.Drawing

    $text = [string]$Cfg.text
    if ([string]::IsNullOrEmpty($text)) {
        $text = " "
    }

    $fontSize = [single](Read-Number $Cfg.fontSize 72.0)
    $fontSize = [Math]::Max(8.0, [Math]::Min(512.0, $fontSize))
    $padding = [single](Read-Number $Cfg.padding 16.0)
    $padding = [Math]::Max(0.0, [Math]::Min(256.0, $padding))
    $autoCanvas = Read-Bool $Cfg.autoCanvas $true
    $canvasWidth = [int][Math]::Max(16, [Math]::Min(8192, [int](Read-Number $Cfg.canvasWidth 512.0)))
    $canvasHeight = [int][Math]::Max(16, [Math]::Min(8192, [int](Read-Number $Cfg.canvasHeight 256.0)))

    $textColor = New-ColorFromArray $Cfg.textColor ([System.Drawing.Color]::White)

    $outlineEnabled = $false
    $outlineWidth = [single]0.0
    $outlineColor = [System.Drawing.Color]::FromArgb(255, 20, 20, 20)
    if ($null -ne $Cfg.outline) {
        $outlineEnabled = Read-Bool $Cfg.outline.enabled $false
        $outlineWidth = [single](Read-Number $Cfg.outline.width 4.0)
        $outlineWidth = [Math]::Max(0.0, [Math]::Min(64.0, $outlineWidth))
        $outlineColor = New-ColorFromArray $Cfg.outline.color $outlineColor
    }

    $shadowEnabled = $false
    $shadowOffsetX = [single]0.0
    $shadowOffsetY = [single]0.0
    $shadowColor = [System.Drawing.Color]::FromArgb(96, 0, 0, 0)
    if ($null -ne $Cfg.shadow) {
        $shadowEnabled = Read-Bool $Cfg.shadow.enabled $false
        $shadowOffsetX = [single](Read-ArrayNumber $Cfg.shadow.offset 0 4.0)
        $shadowOffsetY = [single](Read-ArrayNumber $Cfg.shadow.offset 1 4.0)
        $shadowColor = New-ColorFromArray $Cfg.shadow.color $shadowColor
    }

    $privateFonts = New-Object -TypeName System.Drawing.Text.PrivateFontCollection
    $family = Resolve-FontFamily ([string]$Cfg.fontPath) $privateFonts
    $fontStyle = [System.Drawing.FontStyle]::Bold
    $format = New-StringFormat

    $outlinePad = [single]0.0
    if ($outlineEnabled) {
        $outlinePad = $outlineWidth
    }
    $edgePadding = [single]([Math]::Ceiling($padding + $outlinePad))

    $shadowPadX = [single]0.0
    $shadowPadY = [single]0.0
    if ($shadowEnabled) {
        $shadowPadX = [single][Math]::Abs($shadowOffsetX)
        $shadowPadY = [single][Math]::Abs($shadowOffsetY)
    }

    if ($autoCanvas) {
        $layoutWidth = [single]4096.0
        $layoutHeight = [single]4096.0
    }
    else {
        $layoutWidth = [single][Math]::Max(1.0, $canvasWidth - (($edgePadding + $shadowPadX) * 2.0))
        $layoutHeight = [single][Math]::Max(1.0, $canvasHeight - (($edgePadding + $shadowPadY) * 2.0))
    }

    $measureRect = New-Object -TypeName System.Drawing.RectangleF -ArgumentList 0.0, 0.0, $layoutWidth, $layoutHeight
    $measurePath = New-TextPath $text $family $fontStyle $fontSize $measureRect $format
    $bounds = $measurePath.GetBounds()

    if ($bounds.Width -le 0.0 -or $bounds.Height -le 0.0) {
        $bounds = New-Object -TypeName System.Drawing.RectangleF -ArgumentList 0.0, 0.0, ([Math]::Max(1.0, $fontSize)), ([Math]::Max(1.0, $fontSize))
    }

    if ($autoCanvas) {
        $width = [int][Math]::Ceiling($bounds.Width + ($edgePadding * 2.0) + $shadowPadX)
        $height = [int][Math]::Ceiling($bounds.Height + ($edgePadding * 2.0) + $shadowPadY)
        $width = [Math]::Max(16, [Math]::Min(8192, $width))
        $height = [Math]::Max(16, [Math]::Min(8192, $height))
    }
    else {
        $width = $canvasWidth
        $height = $canvasHeight
    }

    $bitmap = New-Object -TypeName System.Drawing.Bitmap -ArgumentList $width, $height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.PageUnit = [System.Drawing.GraphicsUnit]::Pixel
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $negativeShadowX = [single]0.0
    $negativeShadowY = [single]0.0
    if ($shadowEnabled) {
        $negativeShadowX = [single][Math]::Max(0.0, -$shadowOffsetX)
        $negativeShadowY = [single][Math]::Max(0.0, -$shadowOffsetY)
    }

    $originX = [single]($edgePadding - $bounds.X + $negativeShadowX)
    $originY = [single]($edgePadding - $bounds.Y + $negativeShadowY)
    $drawRect = New-Object -TypeName System.Drawing.RectangleF -ArgumentList $originX, $originY, $layoutWidth, $layoutHeight
    $drawPath = New-TextPath $text $family $fontStyle $fontSize $drawRect $format

    if ($shadowEnabled -and $shadowColor.A -gt 0) {
        $shadowPath = $drawPath.Clone()
        $matrix = New-Object -TypeName System.Drawing.Drawing2D.Matrix
        $matrix.Translate($shadowOffsetX, $shadowOffsetY)
        $shadowPath.Transform($matrix)
        $shadowBrush = New-Object -TypeName System.Drawing.SolidBrush -ArgumentList $shadowColor
        $graphics.FillPath($shadowBrush, $shadowPath)
        $shadowBrush.Dispose()
        $matrix.Dispose()
        $shadowPath.Dispose()
    }

    if ($outlineEnabled -and $outlineWidth -gt 0.0 -and $outlineColor.A -gt 0) {
        $outlinePen = New-Object -TypeName System.Drawing.Pen -ArgumentList $outlineColor, $outlineWidth
        $outlinePen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
        $outlinePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $outlinePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $graphics.DrawPath($outlinePen, $drawPath)
        $outlinePen.Dispose()
    }

    $textBrush = New-Object -TypeName System.Drawing.SolidBrush -ArgumentList $textColor
    $graphics.FillPath($textBrush, $drawPath)
    $textBrush.Dispose()

    $outputDir = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($outputDir)) {
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    }

    $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)

    $drawPath.Dispose()
    $measurePath.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    $format.Dispose()
    $privateFonts.Dispose()

    return @{ width = $width; height = $height }
}

try {
    if ($Command -ne "render") {
        throw "Unknown command: $Command"
    }
    if ([string]::IsNullOrWhiteSpace($config)) {
        throw "config path is required."
    }
    if ([string]::IsNullOrWhiteSpace($out)) {
        throw "out path is required."
    }

    $cfg = Get-Content -LiteralPath $config -Raw -Encoding UTF8 | ConvertFrom-Json
    $result = Save-TransparentTextPng $cfg $out

    if (-not [string]::IsNullOrWhiteSpace([string]$cfg.reportPath)) {
        $reportDir = Split-Path -Parent ([string]$cfg.reportPath)
        if (-not [string]::IsNullOrWhiteSpace($reportDir)) {
            New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
        }
        [ordered]@{
            width = [int]$result.width
            height = [int]$result.height
            output = $out
        } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ([string]$cfg.reportPath) -Encoding UTF8
    }

    exit 0
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}

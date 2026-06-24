param(
    [string]$Name = "terrain",
    [int]$Resolution = 32,
    [double]$SizeX = 40.0,
    [double]$SizeZ = 40.0,
    [double]$Height = 4.0,
    [int]$Seed = 1,
    [double]$NoiseScale = 0.18,
    [int]$SmoothSteps = 2,
    [double]$Terrace = 0.0,
    [string]$HeightMapPath = "",
    [double]$HeightMapStrength = 1.0,
    [switch]$InvertHeightMap,
    [switch]$GeneratePaintMap,
    [string]$PaintPreset = "Grass",
    [double]$PaintStrength = 1.0,
    [string]$OutputRoot = "Resources/3DModel/GeneratedTerrain"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Clamp-Int([int]$value, [int]$min, [int]$max) {
    if ($value -lt $min) { return $min }
    if ($value -gt $max) { return $max }
    return $value
}

function Clamp-Double([double]$value, [double]$min, [double]$max) {
    if ($value -lt $min) { return $min }
    if ($value -gt $max) { return $max }
    return $value
}

function Sanitize-Name([string]$value) {
    $safe = $value -replace '[^A-Za-z0-9_\-]', '_'
    if ([string]::IsNullOrWhiteSpace($safe)) { return "terrain" }
    return $safe
}

function Fract([double]$value) {
    return $value - [Math]::Floor($value)
}

function Hash-Noise([int]$x, [int]$z, [int]$seedValue) {
    return Fract([Math]::Sin($x * 12.9898 + $z * 78.233 + $seedValue * 37.719) * 43758.5453123)
}

function SmoothStep([double]$t) {
    return $t * $t * (3.0 - 2.0 * $t)
}

function Lerp([double]$a, [double]$b, [double]$t) {
    return $a + ($b - $a) * $t
}

function Value-Noise([double]$x, [double]$z, [int]$seedValue) {
    $x0 = [Math]::Floor($x)
    $z0 = [Math]::Floor($z)
    $tx = SmoothStep (Fract $x)
    $tz = SmoothStep (Fract $z)

    $a = Hash-Noise $x0 $z0 $seedValue
    $b = Hash-Noise ($x0 + 1) $z0 $seedValue
    $c = Hash-Noise $x0 ($z0 + 1) $seedValue
    $d = Hash-Noise ($x0 + 1) ($z0 + 1) $seedValue

    $ab = Lerp $a $b $tx
    $cd = Lerp $c $d $tx
    return Lerp $ab $cd $tz
}

function Fractal-Noise([double]$x, [double]$z, [int]$seedValue) {
    $sum = 0.0
    $amp = 1.0
    $freq = 1.0
    $ampSum = 0.0
    for ($o = 0; $o -lt 4; $o++) {
        $sum += (Value-Noise ($x * $freq) ($z * $freq) ($seedValue + $o * 17)) * $amp
        $ampSum += $amp
        $amp *= 0.5
        $freq *= 2.0
    }
    if ($ampSum -le 0.0) { return 0.0 }
    return $sum / $ampSum
}

function Resolve-OptionalPath([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path)) { return "" }
    if (Test-Path -LiteralPath $path) {
        return (Resolve-Path -LiteralPath $path).Path
    }
    $workspacePath = Join-Path (Get-Location) $path
    if (Test-Path -LiteralPath $workspacePath) {
        return (Resolve-Path -LiteralPath $workspacePath).Path
    }
    throw "Height map file was not found: $path"
}

function Mix-Color([System.Drawing.Color]$a, [System.Drawing.Color]$b, [double]$t) {
    $t = Clamp-Double $t 0.0 1.0
    $r = [int][Math]::Round((Lerp $a.R $b.R $t))
    $g = [int][Math]::Round((Lerp $a.G $b.G $t))
    $bb = [int][Math]::Round((Lerp $a.B $b.B $t))
    return [System.Drawing.Color]::FromArgb(255, $r, $g, $bb)
}

function Add-Noise-ToColor([System.Drawing.Color]$color, [double]$amount) {
    $delta = [int][Math]::Round($amount)
    $r = Clamp-Int ($color.R + $delta) 0 255
    $g = Clamp-Int ($color.G + $delta) 0 255
    $b = Clamp-Int ($color.B + $delta) 0 255
    return [System.Drawing.Color]::FromArgb(255, $r, $g, $b)
}

$Resolution = Clamp-Int $Resolution 2 256
$SizeX = Clamp-Double $SizeX 1.0 10000.0
$SizeZ = Clamp-Double $SizeZ 1.0 10000.0
$Height = Clamp-Double $Height 0.0 1000.0
$NoiseScale = Clamp-Double $NoiseScale 0.001 10.0
$SmoothSteps = Clamp-Int $SmoothSteps 0 32
$Terrace = Clamp-Double $Terrace 0.0 64.0
$HeightMapStrength = Clamp-Double $HeightMapStrength 0.0 8.0
$PaintStrength = Clamp-Double $PaintStrength 0.0 1.0

$safeName = Sanitize-Name $Name
$outputDir = Join-Path $OutputRoot $safeName
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$heightBitmap = $null
$resolvedHeightMapPath = ""
if (-not [string]::IsNullOrWhiteSpace($HeightMapPath)) {
    $resolvedHeightMapPath = Resolve-OptionalPath $HeightMapPath
    $heightBitmap = [System.Drawing.Bitmap]::new($resolvedHeightMapPath)
}

function Sample-HeightMap([System.Drawing.Bitmap]$bitmap, [double]$u, [double]$v, [bool]$invert) {
    if ($null -eq $bitmap) { return 0.5 }
    $u = Clamp-Double $u 0.0 1.0
    $v = Clamp-Double $v 0.0 1.0
    $px = [int][Math]::Round($u * ($bitmap.Width - 1))
    $py = [int][Math]::Round((1.0 - $v) * ($bitmap.Height - 1))
    $color = $bitmap.GetPixel($px, $py)
    $gray = ($color.R * 0.299 + $color.G * 0.587 + $color.B * 0.114) / 255.0
    if ($invert) { $gray = 1.0 - $gray }
    return $gray
}

$heights = New-Object 'double[,]' ($Resolution + 1), ($Resolution + 1)

for ($z = 0; $z -le $Resolution; $z++) {
    for ($x = 0; $x -le $Resolution; $x++) {
        $u = $x / [double]$Resolution
        $v = $z / [double]$Resolution
        $nx = ($u - 0.5) * $SizeX
        $nz = ($v - 0.5) * $SizeZ
        $base = Fractal-Noise ($nx * $NoiseScale) ($nz * $NoiseScale) $Seed
        $softHill = [Math]::Sin(($nx + $Seed) * 0.08) * 0.12 + [Math]::Cos(($nz - $Seed) * 0.06) * 0.10
        $h = (($base - 0.5) * 2.0 + $softHill) * $Height
        if ($heightBitmap -ne $null) {
            $heightSample = Sample-HeightMap $heightBitmap $u $v $InvertHeightMap.IsPresent
            $h += (($heightSample - 0.5) * 2.0) * $Height * $HeightMapStrength
        }
        if ($Terrace -gt 0.001) {
            $step = [Math]::Max(0.001, [Math]::Max(0.001, $Height) / $Terrace)
            $h = [Math]::Round($h / $step) * $step
        }
        $heights[$x, $z] = $h
    }
}

for ($s = 0; $s -lt $SmoothSteps; $s++) {
    $next = New-Object 'double[,]' ($Resolution + 1), ($Resolution + 1)
    for ($z = 0; $z -le $Resolution; $z++) {
        for ($x = 0; $x -le $Resolution; $x++) {
            $sum = 0.0
            $count = 0
            for ($oz = -1; $oz -le 1; $oz++) {
                for ($ox = -1; $ox -le 1; $ox++) {
                    $sx = Clamp-Int ($x + $ox) 0 $Resolution
                    $sz = Clamp-Int ($z + $oz) 0 $Resolution
                    $sum += $heights[$sx, $sz]
                    $count++
                }
            }
            $next[$x, $z] = $sum / [double]$count
        }
    }
    $heights = $next
}

$minHeight = [double]::PositiveInfinity
$maxHeight = [double]::NegativeInfinity
for ($z = 0; $z -le $Resolution; $z++) {
    for ($x = 0; $x -le $Resolution; $x++) {
        $h = $heights[$x, $z]
        if ($h -lt $minHeight) { $minHeight = $h }
        if ($h -gt $maxHeight) { $maxHeight = $h }
    }
}
if ([double]::IsNaN($minHeight) -or [double]::IsInfinity($minHeight)) { $minHeight = 0.0 }
if ([double]::IsNaN($maxHeight) -or [double]::IsInfinity($maxHeight)) { $maxHeight = 0.0 }

$objPath = Join-Path $outputDir ($safeName + ".obj")
$reportPath = Join-Path "Resources/.cache/terrain_builder" "latest_report.json"
$metaPath = Join-Path $outputDir ($safeName + "_terrain.json")
$paintPath = Join-Path $outputDir ($safeName + "_paint.png")
New-Item -ItemType Directory -Force -Path (Split-Path $reportPath -Parent) | Out-Null

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# Generated by GE3 Terrain Mesh Builder")
$lines.Add("o $safeName")
$lines.Add("usemtl TerrainMaterial")

for ($z = 0; $z -le $Resolution; $z++) {
    for ($x = 0; $x -le $Resolution; $x++) {
        $px = ($x / [double]$Resolution - 0.5) * $SizeX
        $py = $heights[$x, $z]
        $pz = ($z / [double]$Resolution - 0.5) * $SizeZ
        $lines.Add(("v {0:F6} {1:F6} {2:F6}" -f $px, $py, $pz))
    }
}

for ($z = 0; $z -le $Resolution; $z++) {
    for ($x = 0; $x -le $Resolution; $x++) {
        $u = $x / [double]$Resolution
        $v = $z / [double]$Resolution
        $lines.Add(("vt {0:F6} {1:F6}" -f $u, $v))
    }
}

for ($z = 0; $z -le $Resolution; $z++) {
    for ($x = 0; $x -le $Resolution; $x++) {
        $xm = Clamp-Int ($x - 1) 0 $Resolution
        $xp = Clamp-Int ($x + 1) 0 $Resolution
        $zm = Clamp-Int ($z - 1) 0 $Resolution
        $zp = Clamp-Int ($z + 1) 0 $Resolution
        $heightRight = $heights[$xp, $z]
        $heightLeft = $heights[$xm, $z]
        $heightForward = $heights[$x, $zp]
        $heightBack = $heights[$x, $zm]
        $dx = ($heightRight - $heightLeft) / [Math]::Max(0.0001, $SizeX / $Resolution)
        $dz = ($heightForward - $heightBack) / [Math]::Max(0.0001, $SizeZ / $Resolution)
        $nx = -$dx
        $ny = 2.0
        $nz = -$dz
        $len = [Math]::Sqrt($nx * $nx + $ny * $ny + $nz * $nz)
        if ($len -le 0.0001) { $len = 1.0 }
        $lines.Add(("vn {0:F6} {1:F6} {2:F6}" -f ($nx / $len), ($ny / $len), ($nz / $len)))
    }
}

function Index-At([int]$x, [int]$z, [int]$resolution) {
    return $z * ($resolution + 1) + $x + 1
}

for ($z = 0; $z -lt $Resolution; $z++) {
    for ($x = 0; $x -lt $Resolution; $x++) {
        $a = Index-At $x $z $Resolution
        $b = Index-At ($x + 1) $z $Resolution
        $c = Index-At $x ($z + 1) $Resolution
        $d = Index-At ($x + 1) ($z + 1) $Resolution
        $lines.Add("f $a/$a/$a $c/$c/$c $b/$b/$b")
        $lines.Add("f $b/$b/$b $c/$c/$c $d/$d/$d")
    }
}

[System.IO.File]::WriteAllLines($objPath, $lines, [System.Text.UTF8Encoding]::new($false))

$paintRelativePath = ""
if ($GeneratePaintMap.IsPresent) {
    $bitmapSize = [Math]::Min(1024, [Math]::Max(128, ($Resolution + 1) * 4))
    $paintBitmap = [System.Drawing.Bitmap]::new($bitmapSize, $bitmapSize, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $grass = [System.Drawing.Color]::FromArgb(255, 96, 176, 82)
    $sand = [System.Drawing.Color]::FromArgb(255, 218, 192, 121)
    $rock = [System.Drawing.Color]::FromArgb(255, 131, 124, 112)
    $snow = [System.Drawing.Color]::FromArgb(255, 229, 236, 226)
    $moss = [System.Drawing.Color]::FromArgb(255, 70, 140, 72)

    for ($py = 0; $py -lt $bitmapSize; $py++) {
        for ($px = 0; $px -lt $bitmapSize; $px++) {
            $u = $px / [double]($bitmapSize - 1)
            $v = 1.0 - ($py / [double]($bitmapSize - 1))
            $hx = Clamp-Int ([int][Math]::Round($u * $Resolution)) 0 $Resolution
            $hz = Clamp-Int ([int][Math]::Round($v * $Resolution)) 0 $Resolution
            $h = $heights[$hx, $hz]
            $height01 = 0.5
            if (($maxHeight - $minHeight) -gt 0.0001) {
                $height01 = ($h - $minHeight) / ($maxHeight - $minHeight)
            }
            $xm = Clamp-Int ($hx - 1) 0 $Resolution
            $xp = Clamp-Int ($hx + 1) 0 $Resolution
            $zm = Clamp-Int ($hz - 1) 0 $Resolution
            $zp = Clamp-Int ($hz + 1) 0 $Resolution
            $heightRight = $heights[$xp, $hz]
            $heightLeft = $heights[$xm, $hz]
            $heightForward = $heights[$hx, $zp]
            $heightBack = $heights[$hx, $zm]
            $slope = [Math]::Abs($heightRight - $heightLeft) + [Math]::Abs($heightForward - $heightBack)
            $slope01 = Clamp-Double ($slope / [Math]::Max(0.001, $Height * 0.45)) 0.0 1.0
            $noise = (Fractal-Noise ($u * 12.0) ($v * 12.0) ($Seed + 91)) - 0.5

            switch ($PaintPreset.ToLowerInvariant()) {
                "sand" {
                    $baseColor = Mix-Color $sand $grass (Clamp-Double (($height01 - 0.25) * 1.6) 0.0 1.0)
                    $baseColor = Mix-Color $baseColor $rock ($slope01 * 0.65)
                }
                "rock" {
                    $baseColor = Mix-Color $rock $grass (Clamp-Double ((1.0 - $slope01) * 0.35) 0.0 1.0)
                    $baseColor = Mix-Color $baseColor $snow (Clamp-Double (($height01 - 0.78) * 3.5) 0.0 1.0)
                }
                default {
                    $low = Mix-Color $sand $grass (Clamp-Double ($height01 * 2.0) 0.0 1.0)
                    $mid = Mix-Color $low $moss (Clamp-Double (($height01 - 0.35) * 1.4) 0.0 1.0)
                    $baseColor = Mix-Color $mid $rock ($slope01 * 0.55)
                }
            }
            $detailColor = Add-Noise-ToColor $baseColor ($noise * 30.0 * $PaintStrength)
            $paintBitmap.SetPixel($px, $py, $detailColor)
        }
    }
    $paintBitmap.Save($paintPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $paintBitmap.Dispose()
    $paintRelativePath = ($paintPath -replace '\\', '/')
}

$modelName = "GeneratedTerrain/$safeName"
$vertexCount = ($Resolution + 1) * ($Resolution + 1)
$triangleCount = $Resolution * $Resolution * 2

$heightSamples = New-Object System.Collections.Generic.List[object]
for ($z = 0; $z -le $Resolution; $z++) {
    $row = New-Object System.Collections.Generic.List[double]
    for ($x = 0; $x -le $Resolution; $x++) {
        $sampleHeight = $heights[$x, $z]
        $row.Add([Math]::Round($sampleHeight, 4))
    }
    $heightSamples.Add($row)
}

$report = [ordered]@{
    tool = "TerrainMeshBuilder"
    name = $safeName
    modelName = $modelName
    objPath = (Resolve-Path $objPath).Path
    relativeObjPath = ($objPath -replace '\\', '/')
    terrainCollisionPath = ($metaPath -replace '\\', '/')
    paintMapPath = $paintRelativePath
    heightMapPath = $resolvedHeightMapPath
    resolution = $Resolution
    sizeX = $SizeX
    sizeZ = $SizeZ
    height = $Height
    minHeight = [Math]::Round($minHeight, 6)
    maxHeight = [Math]::Round($maxHeight, 6)
    seed = $Seed
    noiseScale = $NoiseScale
    smoothSteps = $SmoothSteps
    terrace = $Terrace
    paintPreset = $PaintPreset
    vertexCount = $vertexCount
    triangleCount = $triangleCount
}

$meta = [ordered]@{}
foreach ($key in $report.Keys) {
    $meta[$key] = $report[$key]
}
$meta["heightSamples"] = $heightSamples

$report | ConvertTo-Json -Depth 8 | Set-Content -Path $reportPath -Encoding UTF8
$meta | ConvertTo-Json -Depth 12 | Set-Content -Path $metaPath -Encoding UTF8

if ($heightBitmap -ne $null) {
    $heightBitmap.Dispose()
}

Write-Host "Terrain generated: $modelName ($vertexCount vertices, $triangleCount triangles)"

param(
    [string]$OutputDir = "Resources\texture\BakedShader",
    [string]$Texconv = "Resources\tools\Texconv.exe",
    [int]$Size = 512,
    [string[]]$Preset = @("all"),
    [switch]$Force,
    [switch]$NoDDS,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

Set-Location (Resolve-Path (Join-Path $PSScriptRoot ".."))

if ($Size -lt 64) {
    throw "Size must be 64 or larger."
}

Add-Type -AssemblyName System.Drawing

$generatorSource = @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class ShaderTextureBakeGenerator
{
    private const double Tau = Math.PI * 2.0;

    private static double Saturate(double v)
    {
        if (v < 0.0) return 0.0;
        if (v > 1.0) return 1.0;
        return v;
    }

    private static double Frac(double v)
    {
        return v - Math.Floor(v);
    }

    private static double Lerp(double a, double b, double t)
    {
        return a + (b - a) * t;
    }

    private static double SmoothStep(double a, double b, double x)
    {
        double t = Saturate((x - a) / Math.Max(b - a, 0.000001));
        return t * t * (3.0 - 2.0 * t);
    }

    private static double Hash(double x, double y)
    {
        return Frac(Math.Sin(x * 127.1 + y * 311.7) * 43758.5453123);
    }

    private static double Noise(double x, double y)
    {
        double ix = Math.Floor(x);
        double iy = Math.Floor(y);
        double fx = Frac(x);
        double fy = Frac(y);
        double ux = fx * fx * (3.0 - 2.0 * fx);
        double uy = fy * fy * (3.0 - 2.0 * fy);

        double a = Hash(ix, iy);
        double b = Hash(ix + 1.0, iy);
        double c = Hash(ix, iy + 1.0);
        double d = Hash(ix + 1.0, iy + 1.0);

        return Lerp(Lerp(a, b, ux), Lerp(c, d, ux), uy);
    }

    private static double Fbm(double x, double y)
    {
        double value = 0.0;
        double amp = 0.5;
        for (int i = 0; i < 5; ++i)
        {
            value += Noise(x, y) * amp;
            double nx = x * 2.03 + 19.13;
            double ny = y * 2.01 + 7.71;
            x = nx;
            y = ny;
            amp *= 0.5;
        }
        return Saturate(value);
    }

    private static double PeriodicRipple(double u, double v, double scale, double bend)
    {
        double a = Math.Sin(Tau * (u * scale + Math.Sin(Tau * v * 2.0) * bend));
        double b = Math.Sin(Tau * (v * (scale * 0.7) + Math.Sin(Tau * u * 3.0) * bend));
        return a * 0.5 + b * 0.5;
    }

    private static double SegmentDistance(double px, double py, double ax, double ay, double bx, double by)
    {
        double vx = bx - ax;
        double vy = by - ay;
        double wx = px - ax;
        double wy = py - ay;
        double denom = vx * vx + vy * vy;
        if (denom < 0.000001)
        {
            double dx = px - ax;
            double dy = py - ay;
            return Math.Sqrt(dx * dx + dy * dy);
        }

        double t = Saturate((wx * vx + wy * vy) / denom);
        double cx = ax + vx * t;
        double cy = ay + vy * t;
        double ddx = px - cx;
        double ddy = py - cy;
        return Math.Sqrt(ddx * ddx + ddy * ddy);
    }

    private static byte ToByte(double value)
    {
        return (byte)Math.Round(Saturate(value) * 255.0);
    }

    private static void PutPixel(byte[] pixels, int stride, int x, int y, double r, double g, double b, double a)
    {
        int index = y * stride + x * 4;
        pixels[index + 0] = ToByte(b);
        pixels[index + 1] = ToByte(g);
        pixels[index + 2] = ToByte(r);
        pixels[index + 3] = ToByte(a);
    }

    public static void Generate(string kind, string outputPath, int size)
    {
        using (Bitmap bitmap = new Bitmap(size, size, PixelFormat.Format32bppArgb))
        {
            Rectangle rect = new Rectangle(0, 0, size, size);
            BitmapData data = bitmap.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            int bytes = Math.Abs(data.Stride) * data.Height;
            byte[] pixels = new byte[bytes];

            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    double u = (x + 0.5) / size;
                    double v = (y + 0.5) / size;
                    double r = 0.0;
                    double g = 0.0;
                    double b = 0.0;
                    double a = 1.0;

                    if (kind == "water_foam_mask")
                    {
                        double rippleA = Math.Abs(PeriodicRipple(u, v, 5.0, 0.17));
                        double rippleB = Math.Abs(PeriodicRipple(u + 0.27, v - 0.13, 8.0, 0.10));
                        double foam = SmoothStep(0.63, 0.95, rippleA) * 0.58 + SmoothStep(0.75, 0.98, rippleB) * 0.42;
                        double caustic = Math.Pow(Saturate(1.0 - Math.Abs(rippleA - rippleB)), 7.0);
                        double flow = 0.5 + 0.5 * Math.Sin(Tau * (u * 2.0 + v * 0.5 + rippleB * 0.16));
                        r = foam;
                        g = caustic;
                        b = flow;
                    }
                    else if (kind == "water_flow_noise")
                    {
                        double n1 = Fbm(u * 5.0, v * 5.0);
                        double n2 = Fbm(u * 9.0 + 11.3, v * 9.0 - 3.7);
                        double angle = Tau * (n1 * 0.72 + n2 * 0.28);
                        r = 0.5 + Math.Cos(angle) * 0.5;
                        g = 0.5 + Math.Sin(angle) * 0.5;
                        b = SmoothStep(0.52, 0.88, n2);
                    }
                    else if (kind == "gate_swirl_mask")
                    {
                        double px = u * 2.0 - 1.0;
                        double py = v * 2.0 - 1.0;
                        px *= 0.78;
                        double radius = Math.Sqrt(px * px + py * py);
                        double angle = Math.Atan2(py, px);
                        double portal = 1.0 - SmoothStep(0.84, 1.0, radius);
                        double edge = 1.0 - SmoothStep(0.06, 0.18, Math.Abs(radius - 0.72));
                        double arms = 0.5 + 0.5 * Math.Sin(angle * 3.0 + radius * 13.0 + Fbm(u * 4.0, v * 4.0) * 3.4);
                        double core = Math.Pow(Saturate(1.0 - radius), 2.0);
                        r = SmoothStep(0.45, 0.95, arms) * portal;
                        g = edge * portal;
                        b = core * portal;
                        a = portal;
                    }
                    else if (kind == "fire_flame_mask")
                    {
                        double px = u * 2.0 - 1.0;
                        double height = 1.0 - v;
                        double side = Math.Abs(px);
                        double n = Fbm(u * 4.5 + height * 0.7, height * 6.0);
                        double tongue = Fbm(side * 7.0 + n * 1.6, height * 9.0);
                        double width = Lerp(0.92, 0.12, SmoothStep(0.05, 1.0, height));
                        width += (n - 0.5) * 0.16;
                        double edge = side + (tongue - 0.5) * 0.26;
                        double mask = 1.0 - SmoothStep(width - 0.08, width + 0.14, edge);
                        mask *= SmoothStep(0.00, 0.08, height);
                        mask *= 1.0 - SmoothStep(0.93, 1.0, height) * 0.45;
                        double heat = Saturate(0.22 + (1.0 - height) * 0.16 + n * 0.34 + tongue * 0.34);
                        double core = SmoothStep(0.62, 0.92, heat + (1.0 - side) * 0.20) * mask;
                        double ember = SmoothStep(0.78, 0.96, Fbm(u * 19.0, height * 18.0) + (1.0 - height) * 0.15) * mask;
                        r = heat * mask;
                        g = core;
                        b = ember;
                        a = mask;
                    }
                    else if (kind == "fire_orb_mask")
                    {
                        double px = u * 2.0 - 1.0;
                        double py = v * 2.0 - 1.0;
                        double radius = Math.Sqrt(px * px + py * py);
                        double mask = 1.0 - SmoothStep(0.88, 1.03, radius + (Fbm(u * 5.0, v * 5.0) - 0.5) * 0.08);
                        double heat = Fbm(u * 4.4, v * 4.4) * 0.65 + Fbm(u * 12.0 + 3.1, v * 12.0 - 7.8) * 0.35;
                        double crack = SmoothStep(0.72, 0.96, Fbm(u * 18.0, v * 18.0) + heat * 0.28) * mask;
                        r = heat * mask;
                        g = crack;
                        b = SmoothStep(0.45, 0.90, 1.0 - heat) * mask;
                        a = mask;
                    }
                    else if (kind == "glass_crack_mask")
                    {
                        double crack = 0.0;
                        for (int i = 0; i < 7; ++i)
                        {
                            double cx = 0.50 + (Hash(i, 2.0) - 0.5) * 0.16;
                            double cy = 0.50 + (Hash(i, 5.0) - 0.5) * 0.16;
                            double angle = Tau * (i / 7.0) + (Hash(i, 9.0) - 0.5) * 0.55;
                            double len = 0.24 + Hash(i, 12.0) * 0.34;
                            double ex = cx + Math.Cos(angle) * len;
                            double ey = cy + Math.Sin(angle) * len;
                            double d = SegmentDistance(u, v, cx, cy, ex, ey);
                            double line = 1.0 - SmoothStep(0.002, 0.014 + Hash(i, 15.0) * 0.006, d);
                            crack = Math.Max(crack, line);

                            double bx = Lerp(cx, ex, 0.45 + Hash(i, 20.0) * 0.25);
                            double by = Lerp(cy, ey, 0.45 + Hash(i, 23.0) * 0.25);
                            double branchAngle = angle + (Hash(i, 26.0) > 0.5 ? 1.0 : -1.0) * (0.62 + Hash(i, 31.0) * 0.45);
                            double blen = len * (0.25 + Hash(i, 34.0) * 0.20);
                            double bd = SegmentDistance(u, v, bx, by, bx + Math.Cos(branchAngle) * blen, by + Math.Sin(branchAngle) * blen);
                            crack = Math.Max(crack, 1.0 - SmoothStep(0.002, 0.010, bd));
                        }
                        double glow = 1.0 - SmoothStep(0.0, 0.045, Math.Abs(crack - 0.5));
                        double shard = SmoothStep(0.54, 0.90, Fbm(u * 7.0, v * 7.0)) * crack;
                        r = crack;
                        g = Saturate(crack * 0.65 + glow * 0.25);
                        b = shard;
                    }
                    else if (kind == "dash_flow_mask")
                    {
                        double lane = Math.Abs(u - 0.5);
                        double rails = 1.0 - SmoothStep(0.012, 0.045, Math.Min(Math.Abs(lane - 0.35), Math.Abs(lane - 0.43)));
                        double cell = Frac(v * 3.0);
                        double arrowY = 1.0 - Math.Abs(cell * 2.0 - 1.0);
                        double arrowWidth = Lerp(0.10, 0.44, arrowY);
                        double arrow = (1.0 - SmoothStep(arrowWidth - 0.03, arrowWidth + 0.03, lane)) * SmoothStep(0.10, 0.28, arrowY);
                        double streak = SmoothStep(0.82, 0.99, Math.Abs(Math.Sin(Tau * (v * 9.0 + u * 0.55))));
                        r = Saturate(arrow);
                        g = Saturate(rails);
                        b = Saturate(streak * (1.0 - lane));
                    }
                    else
                    {
                        r = u;
                        g = v;
                        b = 0.0;
                    }

                    PutPixel(pixels, data.Stride, x, y, r, g, b, a);
                }
            }

            Marshal.Copy(pixels, 0, data.Scan0, bytes);
            bitmap.UnlockBits(data);
            bitmap.Save(outputPath, ImageFormat.Png);
        }
    }
}
"@

Add-Type -TypeDefinition $generatorSource -ReferencedAssemblies "System.Drawing.dll"

function Convert-ToProjectPath([string]$Path) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $rootPath = [System.IO.Path]::GetFullPath((Get-Location).Path)
    if ($fullPath.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        $fullPath = $fullPath.Substring($rootPath.Length).TrimStart("\", "/")
    }
    return ($fullPath -replace "\\", "/")
}

function Quote-ProcessArgument([string]$Value) {
    return '"' + ($Value -replace '"', '\"') + '"'
}

function Invoke-TexconvHidden([string]$SourcePath, [string]$OutputDir, [string]$Format) {
    if (-not (Test-Path -LiteralPath $Texconv)) {
        throw "Texconv.exe not found: $Texconv"
    }

    $resolvedTexconv = (Resolve-Path -LiteralPath $Texconv).Path
    $arguments = @(
        "-f", (Quote-ProcessArgument $Format),
        "-y",
        "-m", "0",
        "-o", (Quote-ProcessArgument $OutputDir),
        (Quote-ProcessArgument $SourcePath)
    ) -join " "

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $resolvedTexconv
    $psi.Arguments = $arguments
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    if ($process.ExitCode -ne 0) {
        throw (($stdout + "`n" + $stderr).Trim())
    }
}

$allPresets = @(
    [pscustomobject]@{
        key = "water_foam_mask"
        file = "water_foam_mask"
        description = "Water foam, caustics and flow helper channels."
        channels = "R=foam, G=caustics, B=flow, A=opaque"
    },
    [pscustomobject]@{
        key = "water_flow_noise"
        file = "water_flow_noise"
        description = "Water flow vector and broken surface helper channels."
        channels = "R=flowX, G=flowY, B=surface breakup, A=opaque"
    },
    [pscustomobject]@{
        key = "gate_swirl_mask"
        file = "gate_swirl_mask"
        description = "Portal swirl arms, rim and core helper channels."
        channels = "R=swirl arms, G=rim, B=core, A=portal mask"
    },
    [pscustomobject]@{
        key = "fire_flame_mask"
        file = "fire_flame_mask"
        description = "Billboard flame body helper channels."
        channels = "R=heat, G=core, B=ember, A=flame mask"
    },
    [pscustomobject]@{
        key = "fire_orb_mask"
        file = "fire_orb_mask"
        description = "Fire ball patch, crack and smoke helper channels."
        channels = "R=heat patch, G=crack, B=smoke, A=orb mask"
    },
    [pscustomobject]@{
        key = "glass_crack_mask"
        file = "glass_crack_mask"
        description = "Glass crack and shard helper channels."
        channels = "R=crack, G=edge glow, B=shards, A=opaque"
    },
    [pscustomobject]@{
        key = "dash_flow_mask"
        file = "dash_flow_mask"
        description = "Dash panel arrows, rails and speed streak helper channels."
        channels = "R=arrow, G=rail, B=streak, A=opaque"
    }
)

$selectedKeys = @{}
foreach ($name in $Preset) {
    if ($name -eq "all") {
        foreach ($entry in $allPresets) {
            $selectedKeys[$entry.key] = $true
        }
        break
    }
    $selectedKeys[$name] = $true
}

$selectedPresets = @($allPresets | Where-Object { $selectedKeys.ContainsKey($_.key) })
if ($selectedPresets.Count -eq 0) {
    throw "No shader texture bake presets selected."
}

if (-not $DryRun -and -not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$manifestEntries = @()
$index = 0
foreach ($entry in $selectedPresets) {
    $index++
    $pngPath = Join-Path $OutputDir ($entry.file + ".png")
    $ddsPath = Join-Path $OutputDir ($entry.file + ".dds")
    $needsBake = $Force -or -not (Test-Path -LiteralPath $pngPath) -or -not (Test-Path -LiteralPath $ddsPath)

    if ($DryRun) {
        Write-Host ("[{0}/{1}] {2} dry-run" -f $index, $selectedPresets.Count, $entry.key)
    }
    elseif ($needsBake) {
        Write-Host ("[{0}/{1}] bake {2}" -f $index, $selectedPresets.Count, $entry.key)
        [ShaderTextureBakeGenerator]::Generate($entry.key, [System.IO.Path]::GetFullPath($pngPath), $Size)
        if (-not $NoDDS) {
            Invoke-TexconvHidden $pngPath $OutputDir "BC7_UNORM"
        }
    }
    else {
        Write-Host ("[{0}/{1}] latest {2}" -f $index, $selectedPresets.Count, $entry.key)
    }

    $manifestEntries += [pscustomobject]@{
        key = $entry.key
        description = $entry.description
        channels = $entry.channels
        size = $Size
        png = Convert-ToProjectPath $pngPath
        dds = Convert-ToProjectPath $ddsPath
        format = "BC7_UNORM"
    }
}

if (-not $DryRun) {
    $manifestPath = Join-Path $OutputDir "shader_texture_bake_manifest.json"
    [pscustomobject]@{
        generatedAt = (Get-Date).ToString("o")
        generator = "tools/shader_texture/shader_texture_baker.ps1"
        entries = $manifestEntries
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

Write-Host ("Shader texture bake completed. output={0} count={1}" -f (Convert-ToProjectPath $OutputDir), $selectedPresets.Count)

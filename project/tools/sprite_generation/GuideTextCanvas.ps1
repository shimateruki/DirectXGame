# DirectWriteの行高ではなく、描画された文字の範囲を基準に固定サイズのUI画像へ配置する。
# 先にautoCanvasで全文を描くことで、固定キャンバス下端での文字欠けを防ぐ。
Add-Type -AssemblyName System.Drawing
$guideTexconv = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Resources/tools/Texconv.exe'))
if (-not ('GuideTextCanvas' -as [type])) {
    $drawingReferences = if ($PSVersionTable.PSEdition -eq 'Core') {
        @('System.Drawing.Common', 'System.Drawing.Primitives', 'System.Runtime', 'System.Runtime.InteropServices') +
        @('System.Private.Windows.GdiPlus', 'System.Private.Windows.Core' | Where-Object { Test-Path (Join-Path $PSHOME ($_ + '.dll')) })
    } else { @('System.Drawing') }
    Add-Type -ReferencedAssemblies $drawingReferences -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class GuideTextCanvas {
    public static void Save(string input, string output, int width, int height, bool centered) {
        using (var source = new Bitmap(input))
        using (var target = new Bitmap(width, height, PixelFormat.Format32bppArgb)) {
            var data = source.LockBits(new Rectangle(0, 0, source.Width, source.Height),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            int left = source.Width, top = source.Height, right = -1, bottom = -1;
            try {
                var pixels = new byte[data.Stride * source.Height];
                Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);
                for (int y = 0; y < source.Height; ++y)
                    for (int x = 0; x < source.Width; ++x)
                        if (pixels[y * data.Stride + x * 4 + 3] != 0) {
                            left = Math.Min(left, x); right = Math.Max(right, x);
                            top = Math.Min(top, y); bottom = Math.Max(bottom, y);
                        }
            } finally { source.UnlockBits(data); }
            if (right >= left && bottom >= top) {
                int inkWidth = right - left + 1, inkHeight = bottom - top + 1;
                // 縦横比を保ち、上下左右に最低4pxの透明な余白を確保する。
                float scale = Math.Min(1f, Math.Min((width - 8f) / inkWidth, (height - 8f) / inkHeight));
                int w = Math.Max(1, (int)Math.Round(inkWidth * scale));
                int h = Math.Max(1, (int)Math.Round(inkHeight * scale));
                int x = centered ? (width - w) / 2 : 4;
                int y = (height - h) / 2;
                using (var graphics = Graphics.FromImage(target)) {
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    graphics.DrawImage(source, new Rectangle(x, y, w, h),
                        new Rectangle(left, top, inkWidth, inkHeight), GraphicsUnit.Pixel);
                }
            }
            target.Save(output, ImageFormat.Png);
        }
    }
}
'@
}

function Save-GuideTextCanvas($source, $destination, $width, $height, [bool]$centered = $false) {
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('guide-canvas-' + [guid]::NewGuid().ToString('N') + '.png')
    try {
        [GuideTextCanvas]::Save($source, $temporary, $width, $height, $centered)
        if (-not (Test-Path -LiteralPath $destination) -or
            (Get-FileHash -LiteralPath $temporary).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
            # 読み取り側が旧画像をマップしていても、その内容を直接上書きせず完成画像へ置換する。
            if (Test-Path -LiteralPath $destination) {
                [IO.File]::Replace($temporary, $destination, [NullString]::Value)
            } else {
                [IO.File]::Move($temporary, $destination)
            }
        }
    } finally {
        Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue
    }
    # PNGだけを更新して古いDDSを残さず、実行時とエディターで同じ文字を表示する。
    # 幅950・高さ42等の文字画像はBC圧縮の4px単位に合わないため、非圧縮で保持する。
    $cacheDirectory = Join-Path ([IO.Path]::GetTempPath()) ('guide-dds-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $cacheDirectory | Out-Null
    $temporaryDds = Join-Path $cacheDirectory ([IO.Path]::GetFileNameWithoutExtension($destination) + '.dds')
    try {
        $conversion = & $guideTexconv -nologo -f R8G8B8A8_UNORM_SRGB -y -m 1 -o $cacheDirectory $destination
        if ($LASTEXITCODE -ne 0) { throw "文字画像のDDS生成に失敗しました: $destination`n$conversion" }
        $dds = [IO.Path]::ChangeExtension($destination, '.dds')
        if (Test-Path -LiteralPath $dds) {
            [IO.File]::Replace($temporaryDds, $dds, [NullString]::Value)
        } else {
            [IO.File]::Move($temporaryDds, $dds)
        }
    } finally {
        Remove-Item -LiteralPath $temporaryDds -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $cacheDirectory -ErrorAction SilentlyContinue
    }
}

param()

$ErrorActionPreference = "Stop"
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$outputDirectory = Join-Path $projectRoot "Resources\sprite\ui\control_guide\labels"
$fontPath = Join-Path $projectRoot "Resources\font\MPLUS1p-Medium.ttf"
$textToolCandidates = @(
    (Join-Path $projectRoot "..\generated\outputs\Development\TextPngTool.exe"),
    (Join-Path $projectRoot "..\generated\outputs\Release\TextPngTool.exe"),
    (Join-Path $projectRoot "..\generated\outputs\Debug\TextPngTool.exe")
)
$textTool = $textToolCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$texconv = Join-Path $projectRoot "Resources\tools\Texconv.exe"

if (-not $textTool) {
    throw "TextPngTool.exe が見つかりません。先に Development ビルドを実行してください。"
}
if (-not (Test-Path -LiteralPath $fontPath)) {
    throw "UIフォントが見つかりません: $fontPath"
}
if (-not (Test-Path -LiteralPath $texconv)) {
    throw "Texconv.exe が見つかりません: $texconv"
}

$labels = [ordered]@{
    "absorb" = "吸収"
    "throw" = "投げる"
    "slime_attack" = "スライムアタック"
    "hook_aim" = "フック照準"
    "slime_dive" = "スライムダイブ"
    "puni_straight" = "ぷにストレート"
    "puni_guard" = "ぷにガード"
    "bomb_throw" = "ボムスロー"
    "bomb_place" = "ボム設置"
    "blast_jump" = "爆風ジャンプ"
    "fireball" = "ファイアボール"
    "flame_breath" = "フレイムブレス"
    "blaze_step" = "ブレイズステップ"
    "thunder_chain" = "連続落雷"
    "charged_discharge" = "チャージ放電"
    "thunder_step" = "雷光ステップ"
    "updraft" = "上昇気流"
    "wind_breath" = "暴風ブレス"
    "wind_dash" = "疾風ダッシュ"
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
$tempConfig = Join-Path ([System.IO.Path]::GetTempPath()) ("controls-guide-label-" + [guid]::NewGuid().ToString("N") + ".json")

function Write-TextureMeta([string]$assetPath) {
    $metaPath = $assetPath + ".meta"
    if (Test-Path -LiteralPath $metaPath) {
        return
    }

    $relativePath = [System.IO.Path]::GetFullPath($assetPath).Substring($projectRoot.Length).TrimStart("\", "/") -replace "\\", "/"
    $meta = [ordered]@{
        assetType = "Texture"
        guid = [guid]::NewGuid().ToString("N")
        importSettings = [ordered]@{
            colorSpace = "Auto"
            generateMipmaps = $false
        }
        importer = "TextureImporter"
        source = $relativePath
        version = 1
    } | ConvertTo-Json -Depth 4
    [System.IO.File]::WriteAllText($metaPath, $meta + "`n", [System.Text.UTF8Encoding]::new($false))
}

try {
    foreach ($entry in $labels.GetEnumerator()) {
        $pngPath = Join-Path $outputDirectory ($entry.Key + ".png")
        $config = [ordered]@{
            text = $entry.Value
            fontPath = $fontPath
            fontFamilyName = ""
            fontSize = 46.0
            padding = 12.0
            autoCanvas = $false
            canvasWidth = 512
            canvasHeight = 96
            bold = $true
            textColor = @(0.035, 0.20, 0.30, 1.0)
            outline = [ordered]@{
                enabled = $true
                width = 2.0
                color = @(1.0, 1.0, 1.0, 0.82)
            }
            shadow = [ordered]@{
                enabled = $false
                offset = @(0.0, 0.0)
                color = @(0.0, 0.0, 0.0, 0.0)
            }
        } | ConvertTo-Json -Depth 5
        [System.IO.File]::WriteAllText($tempConfig, $config, [System.Text.UTF8Encoding]::new($false))

        & $textTool render -config $tempConfig -out $pngPath
        if ($LASTEXITCODE -ne 0) {
            throw "技名画像の生成に失敗しました: $($entry.Value)"
        }

        & $texconv -f BC7_UNORM_SRGB -y -m 0 -o $outputDirectory $pngPath | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "DDS変換に失敗しました: $pngPath"
        }

        Write-TextureMeta $pngPath
        Write-TextureMeta ([System.IO.Path]::ChangeExtension($pngPath, ".dds"))
    }
}
finally {
    if (Test-Path -LiteralPath $tempConfig) {
        Remove-Item -LiteralPath $tempConfig -Force
    }
}

Write-Host "操作ガイド用の技名画像を生成しました: $outputDirectory"

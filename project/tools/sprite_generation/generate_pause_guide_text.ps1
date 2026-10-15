<#
ポーズと操作ガイドで使う短い文字を、透明背景の PNG として生成する。
画面の文言とゲーム中の入力案内を、この表から調整できるようにする。
#>
param([string[]]$Names = @())

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$tool = Join-Path $projectRoot '../generated/outputs/Development/TextPngTool.exe'
$font = Join-Path $projectRoot 'Resources/font/MPLUS1p-Medium.ttf'
$output = Join-Path $projectRoot 'Resources/sprite/generated/text/pause_guide'
$request = Join-Path ([IO.Path]::GetTempPath()) ('pause-guide-text-' + [guid]::NewGuid().ToString('N') + '.json')
$utf8 = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'GuideTextCanvas.ps1')
if (-not (Test-Path -LiteralPath $tool)) { throw "TextPngTool.exe がありません: $tool" }
New-Item -ItemType Directory -Force -Path $output | Out-Null

$labels = @(
    @{ name='title'; text='メニュー'; width=680; height=96; size=65 },
    @{ name='resume'; text='ゲームにもどる'; width=620; height=74; size=46 },
    @{ name='retry'; text='はじめからやりなおす'; width=620; height=74; size=42 },
    @{ name='stage_select'; text='ステージをえらぶ'; width=620; height=74; size=44 },
    @{ name='settings'; text='せってい'; width=620; height=74; size=46 },
    @{ name='title_return'; text='タイトルへもどる'; width=620; height=74; size=44 },
    @{ name='keyboard_hint'; text='← ワザへ    ↑↓ えらぶ    SPACE けってい    TAB とじる'; width=1320; height=48; size=31 },
    @{ name='gamepad_hint'; text='← ワザへ    十字キー えらぶ    A けってい    START とじる'; width=1320; height=48; size=31 },
    @{ name='select_keyboard_hint'; text='↑↓ えらぶ    SPACE けってい    TAB とじる'; width=1320; height=48; size=31 },
    @{ name='select_gamepad_hint'; text='十字キー えらぶ    A けってい    START とじる'; width=1320; height=48; size=31 },
    @{ name='page_footer_keyboard'; text='← 操作説明    → メニュー    TAB とじる'; width=1300; height=54; size=31 },
    @{ name='page_footer_gamepad'; text='← 操作説明    → メニュー    START とじる'; width=1300; height=54; size=31 },
    @{ name='basic_footer_keyboard'; text='→ ワザ図鑑    TAB とじる'; width=1300; height=54; size=31 },
    @{ name='basic_footer_gamepad'; text='→ ワザ図鑑    START とじる'; width=1300; height=54; size=31 }
    ,@{ name='footer_close_keyboard'; text='TAB  とじる'; width=480; height=76; size=43 }
    ,@{ name='footer_close_gamepad'; text='B  とじる'; width=480; height=76; size=43 }
    ,@{ name='footer_confirm_keyboard'; text='SPACE  けってい'; width=480; height=76; size=43 }
    ,@{ name='footer_confirm_gamepad'; text='A  けってい'; width=480; height=76; size=43 }
)

try {
    foreach ($label in $labels) {
        if ($Names.Count -gt 0 -and $label.name -notin $Names) { continue }
        $config = @{
            text=$label.text; fontPath=$font; fontSize=$label.size; padding=3
            autoCanvas=$true; canvasWidth=$label.width; canvasHeight=$label.height
            bold=$true; textColor=@(0.035,0.16,0.23,1.0)
            outline=@{ enabled=$false }; shadow=@{ enabled=$false }
        }
        [IO.File]::WriteAllText($request, ($config | ConvertTo-Json -Depth 5), $utf8)
        $path = Join-Path $output ($label.name + '.png')
        $temporary = $request + '.png'
        & $tool render -config $request -out $temporary | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "文字画像の生成に失敗しました: $($label.name)" }
        # メニュー行・見出しは可視文字を中央へ。図鑑のフッターは本文と同じ左揃え。
        $centered = $label.name -like 'footer_close_*' -or $label.name -like 'footer_confirm_*' -or
                    $label.name -notlike '*footer*'
        Save-GuideTextCanvas $temporary $path $label.width $label.height $centered
        $metaPath = $path + '.meta'
        if (-not (Test-Path -LiteralPath $metaPath)) {
            $relative = 'Resources/sprite/generated/text/pause_guide/' + $label.name + '.png'
            $meta = [ordered]@{
                assetType='Texture'; guid=[guid]::NewGuid().ToString('N')
                importSettings=[ordered]@{ colorSpace='Auto'; generateMipmaps=$false }
                importer='TextureImporter'; source=$relative; version=1
            }
            [IO.File]::WriteAllText($metaPath, ($meta | ConvertTo-Json -Depth 4) + "`n", $utf8)
        }
    }
}
finally {
    Remove-Item -LiteralPath $request -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath ($request + '.png') -ErrorAction SilentlyContinue
}

# 操作説明の見出し・選択案内を、全文を描画してから固定枠へ収めて生成する。
param([string]$TextTool = '')
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $TextTool) { $TextTool = Join-Path $taskRoot '../generated/outputs/Development/TextPngTool.exe' }
. (Join-Path $PSScriptRoot 'GuideTextCanvas.ps1')
$taskDirectory = Join-Path ([IO.Path]::GetTempPath()) ('guide-navigation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskDirectory | Out-Null
try {
    $entries = @(
        @{ Name='guide_choose_heading'; Text='ワザを選ぶ'; Width=360; Height=48; Font=40; Center=$false },
        @{ Name='guide_select_hint'; Text='↑ ↓  ワザを切り替え'; Width=410; Height=48; Font=30; Center=$false },
        @{ Name='guide_select_footer'; Text='↑↓  ワザを選ぶ'; Width=390; Height=50; Font=34; Center=$true }
    )
    $book = Get-Content (Join-Path $taskRoot 'Resources/json/ui/abilityGuideBook.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($property in $book.PSObject.Properties) {
        # 肖像の右側へ左揃えで配置する。長い能力名だけ、文字全体を枠内へ縮小する。
        $entries += @{Name=($property.Name+'/guide_title'); Text=$property.Value.title; Width=450; Height=82; Font=64; Center=$false}
    }
    foreach ($entry in $entries) {
        $config = @{
            text=$entry.Text; fontPath=(Join-Path $taskRoot 'Resources/font/MPLUS1p-Medium.ttf')
            fontSize=$entry.Font; padding=8; autoCanvas=$true; bold=$true
            textColor=@(0.035,0.15,0.23,1.0)
            outline=@{enabled=$false}; shadow=@{enabled=$false}
        }
        $configPath = Join-Path $taskDirectory 'text.json'
        [IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        $image = Join-Path $taskDirectory 'text.png'
        & $TextTool render -config $configPath -out $image | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "文字生成に失敗しました: $($entry.Name)" }
        $destination = Join-Path $taskRoot ('Resources/sprite/ui/control_guide/book/' + $entry.Name + '.png')
        Save-GuideTextCanvas $image $destination $entry.Width $entry.Height $entry.Center
    }
} finally {
    Get-ChildItem -LiteralPath $taskDirectory -File | Remove-Item
    Remove-Item -LiteralPath $taskDirectory
}

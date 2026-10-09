# 王冠最終戦の日本語案内を、プロジェクト標準の文字PNGツールで生成します。
param([switch]$SealsOnly, [switch]$InheritanceOnly, [switch]$ReleaseOnly)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskTool = Join-Path $taskRoot '../generated/outputs/Development/TextPngTool.exe'
$taskFont = Join-Path $taskRoot 'Resources/font/MPLUS1p-Medium.ttf'
$taskOutput = Join-Path $taskRoot 'Resources/sprite/ui/crown_finale'
$taskConfig = Join-Path ([IO.Path]::GetTempPath()) ('crown-finale-' + [guid]::NewGuid().ToString('N') + '.json')
$taskEncoding = [Text.UTF8Encoding]::new($false)
function Render-CrownText($name, $text, $size = 32) {
    $taskData = @{
        text = $text; fontPath = $taskFont; fontSize = $size; padding = 10
        autoCanvas = $true; canvasWidth = 1320; canvasHeight = 150; bold = $true
        textColor = @(1.0, 0.95, 0.75, 1.0)
        outline = @{ enabled = $true; width = 3; color = @(0.06,0.05,0.18,1.0) }
        shadow = @{ enabled = $true; offsetX = 1; offsetY = 3; color = @(0.02,0.02,0.08,0.85) }
    }
    # DDSの圧縮ブロックに合わせ、継承の文字画像は4の倍数のキャンバスへ収めます。
    if ($name -eq 'inherit') {
        $taskData.autoCanvas = $false; $taskData.canvasWidth = 720; $taskData.canvasHeight = 80
    }
    [IO.File]::WriteAllText($taskConfig, ($taskData | ConvertTo-Json -Depth 5), $taskEncoding)
    & $taskTool render -config $taskConfig -out (Join-Path $taskOutput ($name + '.png'))
    if ($LASTEXITCODE -ne 0) { throw "王冠案内の文字生成に失敗: $name" }
}
function Render-CrownBookText($name, $text, $width, $height, $size, $white) {
    $taskColor = if ($white) { @(1,1,1,1) } else { @(0.035,0.15,0.23,1) }
    $taskData = @{
        text = $text; fontPath = $taskFont; fontSize = $size; padding = 3
        autoCanvas = $false; canvasWidth = $width; canvasHeight = $height; bold = $true
        textColor = $taskColor
        outline = @{ enabled = $false }; shadow = @{ enabled = $false }
    }
    [IO.File]::WriteAllText($taskConfig, ($taskData | ConvertTo-Json -Depth 5), $taskEncoding)
    & $taskTool render -config $taskConfig -out (Join-Path $taskOutput ($name + '.png'))
    if ($LASTEXITCODE -ne 0) { throw "王冠ガイドの生成に失敗: $name" }
}

try {
    New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
    if ($ReleaseOnly) {
        Render-CrownText 'release' "最後の封印が砕け、王冠に亀裂が走る――"
        return
    }
    if ($InheritanceOnly) {
        Render-CrownText 'inherit' "落ちた王冠の欠片を、吸い込みで取り込む――"
        return
    }
    Render-CrownText 'seals' "光る封印へ攻撃を誘導！ 狙いが止まったら回避！`n① 槍を当てる　② 着地させる　③ 突進を通す"
    if ($SealsOnly) { return }
    Render-CrownText 'inherit' "落ちた王冠の欠片を、吸い込みで取り込む――"
    Render-CrownText 'boss_intro' "天空城の支配者　偽王スライム" 58
    Render-CrownText 'release' "最後の封印が砕け、王冠に亀裂が走る――"
    Render-CrownText 'awaken' "王冠の力を受け継いだ姿　覇王スライム" 48
    Render-CrownText 'controls' "左クリック：王冠槍 ／ 長押しして離す：槍の雨`n右クリック：王威の波で装甲を開く　E：王翼突進"

    $taskBook = (Get-Content (Join-Path $taskRoot 'Resources/json/ui/abilityGuideBook.json') -Raw -Encoding UTF8 | ConvertFrom-Json).crown
    $taskOutput = Join-Path $taskRoot 'Resources/sprite/ui/control_guide/book/crown'
    New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
    foreach ($taskSlot in @('title','intro','role','traits','tip')) {
        $taskWidth = if ($taskSlot -eq 'title') { 720 } elseif ($taskSlot -in @('traits','tip')) { 1180 } else { 900 }
        $taskHeight = switch ($taskSlot) { 'title' {82} 'intro' {112} 'role' {52} 'traits' {96} 'tip' {104} }
        $taskSize = if ($taskSlot -eq 'title') { 52 } elseif ($taskSlot -eq 'intro') { 34 } else { 29 }
        Render-CrownBookText $taskSlot $taskBook.$taskSlot $taskWidth $taskHeight $taskSize ($taskSlot -eq 'title')
    }
    for ($taskIndex = 0; $taskIndex -lt 3; ++$taskIndex) {
        $taskMove = $taskBook.moves[$taskIndex]
        Render-CrownBookText "move_${taskIndex}_name" $taskMove.name 800 59 38 $false
        Render-CrownBookText "move_${taskIndex}_condition" $taskMove.condition 950 46 26 $false
        Render-CrownBookText "move_${taskIndex}_description" $taskMove.description 1000 90 28 $false
    }

} finally {
    if (Test-Path -LiteralPath $taskConfig) { Remove-Item -LiteralPath $taskConfig }
}

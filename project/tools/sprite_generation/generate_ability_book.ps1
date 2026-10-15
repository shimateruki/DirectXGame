<#
能力ガイドの日本語テキストを TextPngTool で PNG 化する。
UpdateLayout/UpdateBasics を指定すると対応する UI レイアウト JSON も更新する。
#>
param([switch]$UpdateLayout, [switch]$UpdateBasics)

$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput = Join-Path $taskRoot 'Resources/sprite/ui/control_guide/book'
$taskTool = Join-Path $taskRoot '../generated/outputs/Development/TextPngTool.exe'
$taskFont = Join-Path $taskRoot 'Resources/font/MPLUS1p-Medium.ttf'
$taskData = Get-Content (Join-Path $taskRoot 'Resources/json/ui/abilityGuideBook.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$taskConfig = Join-Path ([IO.Path]::GetTempPath()) ('ability-book-' + [guid]::NewGuid().ToString('N') + '.json')
$taskEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'GuideTextCanvas.ps1')

function Render-BookText($path, $text, $width, $height, $size, $color) {
    $config = @{
        text = $text; fontPath = $taskFont; fontSize = $size; padding = 3
        autoCanvas = $true; canvasWidth = $width; canvasHeight = $height
        bold = $true; textColor = $color
        outline = @{ enabled = $false }; shadow = @{ enabled = $false }
    }
    [IO.File]::WriteAllText($taskConfig, ($config | ConvertTo-Json -Depth 6), $taskEncoding)
    # WIC が実行中に参照されている既存 PNG を直接開くと失敗するため、一時画像を完成させてから入れ替える。
    $temporaryImage = Join-Path ([IO.Path]::GetTempPath()) ('ability-text-' + [guid]::NewGuid().ToString('N') + '.png')
    try {
        & $taskTool render -config $taskConfig -out $temporaryImage | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "文字生成に失敗: $path" }
        # 見出しだけ中央揃え。本文は左揃えとし、画像差替え後も同じ配置枠を使う。
        $centered = [IO.Path]::GetFileName($path) -eq 'title.png'
        Save-GuideTextCanvas $temporaryImage $path $width $height $centered
    }
    finally {
        Remove-Item -LiteralPath $temporaryImage -ErrorAction SilentlyContinue
    }
}

try {
    $ink = @(0.035, 0.15, 0.23, 1.0)
    $accent = @(0.66, 0.12, 0.34, 1.0)
    New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
    $basics = Get-Content (Join-Path $taskRoot 'Resources/json/ui/basicGuideBook.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $basicDir = Join-Path $taskOutput 'basics'
    New-Item -ItemType Directory -Force -Path $basicDir | Out-Null
    Render-BookText (Join-Path $basicDir 'title.png') $basics.title 720 82 57 $ink
    Render-BookText (Join-Path $basicDir 'intro.png') $basics.intro 900 112 40 $ink
    Render-BookText (Join-Path $basicDir 'note.png') $basics.note 1180 50 24 $accent
    Render-BookText (Join-Path $basicDir 'footer.png') $basics.footer 1300 54 27 $ink
    Render-BookText (Join-Path $basicDir 'footer_gamepad.png') $basics.footerGamepad 1300 54 27 $ink
    for ($i=0; $i -lt $basics.moves.Count; ++$i) {
        $move = $basics.moves[$i]
        Render-BookText (Join-Path $basicDir "move_${i}_name.png") $move.name 900 50 35 $ink
        Render-BookText (Join-Path $basicDir "move_${i}_condition.png") $move.condition 950 42 34 $accent
        Render-BookText (Join-Path $basicDir "move_${i}_condition_gamepad.png") $move.conditionGamepad 950 42 34 $accent
        Render-BookText (Join-Path $basicDir "move_${i}_description.png") $move.description 1000 44 32 $ink
    }
    foreach ($entry in $taskData.PSObject.Properties) {
        $dir = Join-Path $taskOutput $entry.Name
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $book = $entry.Value
        Render-BookText (Join-Path $dir 'title.png') $book.title 720 82 57 $ink
        Render-BookText (Join-Path $dir 'intro.png') $book.intro 900 120 43 $ink
        Render-BookText (Join-Path $dir 'role.png') $book.role 900 52 29 $accent
        Render-BookText (Join-Path $dir 'traits.png') $book.traits 900 110 40 $ink
        Render-BookText (Join-Path $dir 'tip.png') $book.tip 1180 104 29 $ink
        for ($i = 0; $i -lt 3; ++$i) {
            $move = if ($i -lt $book.moves.Count) { $book.moves[$i] } else { @{name=' '; condition=' '; description=' '} }
            Render-BookText (Join-Path $dir "move_${i}_name.png") $move.name 400 50 36 $ink
            Render-BookText (Join-Path $dir "move_${i}_condition.png") $move.condition 420 42 25 $accent
            Render-BookText (Join-Path $dir "move_${i}_description.png") $move.description 820 90 32 $ink
        }
    }
    Render-BookText (Join-Path $taskOutput 'traits_heading.png') 'このスライムの とくちょう' 1180 60 36 $accent
    Render-BookText (Join-Path $taskOutput 'moves_heading.png') 'ワザ' 660 60 45 $ink
    Render-BookText (Join-Path $taskOutput 'tip_heading.png') 'つかいこなしの ヒント' 1180 60 36 $accent
    Render-BookText (Join-Path $taskOutput 'footer.png') '← 操作説明    → メニュー    Tab：とじる' 1300 54 30 $ink
    Render-BookText (Join-Path $taskOutput 'footer_gamepad.png') '← 操作説明    → メニュー    START：とじる' 1300 54 30 $ink
    Render-BookText (Join-Path $taskOutput 'navigation.png') '← 操作説明    メニュー →' 800 54 30 $ink
    Render-BookText (Join-Path $taskOutput 'navigation_basics.png') '操作説明    ワザ図鑑 →' 800 54 30 $ink
    Render-BookText (Join-Path $taskOutput 'continue.png') 'つづき ↓' 220 48 28 $accent
    Render-BookText (Join-Path $taskOutput 'end.png') 'おわり' 220 48 28 $accent

    if ($UpdateLayout) {
        $layoutPath = Join-Path $taskRoot 'Resources/json/sprite/controlsGuide.json'
        $layout = Get-Content $layoutPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $items = [Collections.Generic.List[object]]::new()
        foreach ($sprite in $layout.sprites) {
            if ($sprite.name -notlike 'controls_guide_page_1_*') { $items.Add($sprite) }
        }
        function Add-BookSprite($name, $texture, $x, $y, $w, $h, $slot='', $scroll=$false, $row=-1) {
            $sprite = [ordered]@{
                name = $name; texture = $texture; position = @($x,$y); size = @($w,$h)
                anchor = @(0.0,0.0); color = @(1.0,1.0,1.0,1.0); emissive = 1.0
            }
            if ($name -like '*_book_*') {
                $sprite.bookElement = $true; $sprite.bookSlot = $slot
                $sprite.bookScroll = $scroll; $sprite.bookRow = $row
            }
            $items.Add($sprite)
        }
        $prefix = 'controls_guide_page_1_'
        $tex = 'ui/control_guide/book/'
        Add-BookSprite ($prefix+'panel') ($tex+'scroll_panel.png') 192 28 1536 1024
        Add-BookSprite ($prefix+'portrait') ($tex+'portraits/normal.png') 1125 295 430 430
        Add-BookSprite ($prefix+'book_title') ($tex+'normal/title.png') 912 132 720 82 'title'
        Add-BookSprite ($prefix+'book_intro') ($tex+'normal/intro.png') 420 284 720 96 'intro'
        Add-BookSprite ($prefix+'book_traits') ($tex+'normal/traits.png') 420 410 720 88 'traits'
        Add-BookSprite ($prefix+'book_moves_heading') ($tex+'moves_heading.png') 420 538 594 54
        for ($i=0; $i -lt 3; ++$i) {
            $y = 610 + $i * 80
            Add-BookSprite ($prefix+"book_move_${i}_glyph") 'ui/input/keyboard/key_e.png' 428 $y 70 60 "glyph_$i" $false $i
            # 技名と現在の入力機器に対応するボタンだけを表示する。
            Add-BookSprite ($prefix+"book_move_${i}_name") ($tex+"normal/move_${i}_name.png") 530 ($y+6) 400 50 "move_${i}_name" $false $i
        }
        Add-BookSprite ($prefix+'book_footer') ($tex+'footer.png') 440 968 1300 54
        foreach ($s in $items) {
            if ($s.name -eq 'controls_guide_page_1_book_scroll_track') { $s.color = @(0.35,0.66,0.75,0.38) }
            if ($s.name -eq 'controls_guide_page_1_book_scroll_thumb') { $s.color = @(1.0,0.36,0.68,1.0) }
            if ($s.name -eq 'controls_guide_background') { $s.color = @(1.0,1.0,1.0,1.0) }
        }
        $layout.sprites = $items.ToArray()
        $layout | Add-Member -Force -NotePropertyName abilityBook -NotePropertyValue ([ordered]@{
            enabled = $true; viewportTop = 0; viewportBottom = 1080; contentBottom = 1080
            scrollSpeed = 650; wheelStep = 125; portraitBob = 4
            dataPath = 'Resources/json/ui/abilityGuideBook.json'
        })
        [IO.File]::WriteAllText($layoutPath, ($layout | ConvertTo-Json -Depth 10) + "`n", $taskEncoding)
    }
    if ($UpdateBasics) {
        # 基本操作だけを更新し、調整済みの能力ページには触れない。
        $layoutPath = Join-Path $taskRoot 'Resources/json/sprite/controlsGuide.json'
        $layout = Get-Content $layoutPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $items = [Collections.Generic.List[object]]::new()
        foreach ($s in $layout.sprites) {
            if ($s.name -notlike 'controls_guide_page_0_*') { $items.Add($s) }
        }
        function Add-BasicSprite($name,$texture,$x,$y,$w,$h) {
            $items.Add([ordered]@{
                name=('controls_guide_page_0_'+$name); texture=$texture
                position=@($x,$y); size=@($w,$h); anchor=@(0.0,0.0)
                color=@(1.0,1.0,1.0,1.0); emissive=1.0
            })
        }
        $tex='ui/control_guide/book/'
        Add-BasicSprite 'panel' ($tex+'scroll_panel.png') 192 28 1536 1024
        Add-BasicSprite 'detail_title' ($tex+'basics/title.png') 912 132 720 82
        Add-BasicSprite 'detail_intro' ($tex+'basics/intro.png') 420 252 900 112
        for($i=0; $i -lt 3; ++$i) {
            $y=390+$i*160
            Add-BasicSprite "detail_card_$i" ($tex+'technique_row.png') 420 $y 1100 150
            $items[$items.Count-1].textureCrop=@(0,125,2172,460)
            $glyph=$basics.moves[$i].glyphSize
            # 3行とも枠の中心をX=516へ揃え、実画像はC++側で縦横比を保って中央配置する。
            $glyphPositions = @(@(448,30), @(443.5,44), @(470,30))
            $glyphSizes = @(@(136,102), @(145,64), @(92,92))
            Add-BasicSprite "action_$i" $basics.moves[$i].glyph $glyphPositions[$i][0] ($y+$glyphPositions[$i][1]) $glyphSizes[$i][0] $glyphSizes[$i][1]
            Add-BasicSprite "detail_name_$i" ($tex+"basics/move_${i}_name.png") 600 ($y+6) 900 50
            Add-BasicSprite "detail_condition_$i" ($tex+"basics/move_${i}_condition.png") 600 ($y+52) 950 42
            Add-BasicSprite "detail_description_$i" ($tex+"basics/move_${i}_description.png") 600 ($y+96) 900 39.6
        }
        Add-BasicSprite 'detail_footer' ($tex+'basics/footer.png') 440 968 1300 54
        $layout.sprites=$items.ToArray()
        [IO.File]::WriteAllText($layoutPath,($layout | ConvertTo-Json -Depth 10)+"`n",$taskEncoding)
    }
    Write-Host '基本操作と能力ガイドの日本語テキストを生成しました。'
} finally {
    if (Test-Path -LiteralPath $taskConfig) { Remove-Item -LiteralPath $taskConfig }
}

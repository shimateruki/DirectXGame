param([switch]$UpdateLayout, [switch]$UpdateBasics)

$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput = Join-Path $taskRoot 'Resources/sprite/ui/control_guide/book'
$taskTool = Join-Path $taskRoot '../generated/outputs/Development/TextPngTool.exe'
$taskFont = Join-Path $taskRoot 'Resources/font/MPLUS1p-Medium.ttf'
$taskData = Get-Content (Join-Path $taskRoot 'Resources/json/ui/abilityGuideBook.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$taskConfig = Join-Path ([IO.Path]::GetTempPath()) ('ability-book-' + [guid]::NewGuid().ToString('N') + '.json')
$taskEncoding = [Text.UTF8Encoding]::new($false)

function Render-BookText($path, $text, $width, $height, $size, $color) {
    $config = @{
        text = $text; fontPath = $taskFont; fontSize = $size; padding = 3
        autoCanvas = $false; canvasWidth = $width; canvasHeight = $height
        bold = $true; textColor = $color
        outline = @{ enabled = $false }; shadow = @{ enabled = $false }
    }
    [IO.File]::WriteAllText($taskConfig, ($config | ConvertTo-Json -Depth 6), $taskEncoding)
    & $taskTool render -config $taskConfig -out $path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "文字生成に失敗: $path" }
}

try {
    $ink = @(0.035, 0.15, 0.23, 1.0)
    $accent = @(0.66, 0.12, 0.34, 1.0)
    New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
    $basics = Get-Content (Join-Path $taskRoot 'Resources/json/ui/basicGuideBook.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $basicDir = Join-Path $taskOutput 'basics'
    New-Item -ItemType Directory -Force -Path $basicDir | Out-Null
    Render-BookText (Join-Path $basicDir 'title.png') $basics.title 720 82 57 @(1,1,1,1)
    Render-BookText (Join-Path $basicDir 'intro.png') $basics.intro 900 112 35 $ink
    Render-BookText (Join-Path $basicDir 'note.png') $basics.note 1180 50 24 $accent
    Render-BookText (Join-Path $basicDir 'footer.png') $basics.footer 1300 54 27 @(0.80,0.96,1,1)
    for ($i=0; $i -lt $basics.moves.Count; ++$i) {
        $move = $basics.moves[$i]
        Render-BookText (Join-Path $basicDir "move_${i}_name.png") $move.name 900 50 35 $ink
        Render-BookText (Join-Path $basicDir "move_${i}_condition.png") $move.condition 950 42 26 $accent
        Render-BookText (Join-Path $basicDir "move_${i}_description.png") $move.description 1000 44 27 $ink
    }
    foreach ($entry in $taskData.PSObject.Properties) {
        $dir = Join-Path $taskOutput $entry.Name
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $book = $entry.Value
        Render-BookText (Join-Path $dir 'title.png') $book.title 720 82 57 @(1,1,1,1)
        Render-BookText (Join-Path $dir 'intro.png') $book.intro 900 112 35 $ink
        Render-BookText (Join-Path $dir 'role.png') $book.role 900 52 29 $accent
        Render-BookText (Join-Path $dir 'traits.png') $book.traits 1180 96 29 $ink
        Render-BookText (Join-Path $dir 'tip.png') $book.tip 1180 104 29 $ink
        for ($i = 0; $i -lt 3; ++$i) {
            $move = if ($i -lt $book.moves.Count) { $book.moves[$i] } else { @{name=' '; condition=' '; description=' '} }
            Render-BookText (Join-Path $dir "move_${i}_name.png") $move.name 800 59 38 $ink
            Render-BookText (Join-Path $dir "move_${i}_condition.png") $move.condition 950 46 26 $accent
            Render-BookText (Join-Path $dir "move_${i}_description.png") $move.description 1000 90 28 $ink
        }
    }
    Render-BookText (Join-Path $taskOutput 'traits_heading.png') 'このスライムの とくちょう' 1180 60 36 $accent
    Render-BookText (Join-Path $taskOutput 'moves_heading.png') 'ワザの つかいかた' 1180 60 38 $ink
    Render-BookText (Join-Path $taskOutput 'tip_heading.png') 'つかいこなしの ヒント' 1180 60 36 $accent
    Render-BookText (Join-Path $taskOutput 'footer.png') 'W/S・上下・ホイール：スクロール    Tab / B：もどる' 1300 54 27 @(0.80,0.96,1,1)
    Render-BookText (Join-Path $taskOutput 'navigation.png') '← 基本操作    能力ガイド →' 800 54 30 @(0.86,0.98,1,1)
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
        Add-BookSprite ($prefix+'panel') ($tex+'scroll_panel.png') 200 34 1536 1024
        Add-BookSprite ($prefix+'portrait') ($tex+'portraits/normal.png') 352 114 380 380
        Add-BookSprite ($prefix+'book_title') ($tex+'normal/title.png') 930 132 720 82 'title'
        Add-BookSprite ($prefix+'book_intro') ($tex+'normal/intro.png') 762 277 850 106 'intro'
        Add-BookSprite ($prefix+'book_role') ($tex+'normal/role.png') 762 402 850 49 'role'
        Add-BookSprite ($prefix+'book_traits_heading') ($tex+'traits_heading.png') 406 460 1120 57 '' $true
        Add-BookSprite ($prefix+'book_traits') ($tex+'normal/traits.png') 406 514 1120 91 'traits' $true
        Add-BookSprite ($prefix+'book_moves_heading') ($tex+'moves_heading.png') 406 616 1120 57 '' $true
        for ($i=0; $i -lt 3; ++$i) {
            $y = 686 + $i * 198
            Add-BookSprite ($prefix+"book_move_${i}_card") ($tex+'technique_row.png') 395 ($y-5) 1180 193 '' $true $i
            $items[$items.Count-1].textureCrop = @(0,125,2172,460)
            Add-BookSprite ($prefix+"book_move_${i}_glyph") 'ui/input/keyboard/key_e.png' 418 ($y+19) 100 100 "glyph_$i" $true $i
            Add-BookSprite ($prefix+"book_move_${i}_name") ($tex+"normal/move_${i}_name.png") 556 ($y+8) 800 55 "move_${i}_name" $true $i
            Add-BookSprite ($prefix+"book_move_${i}_condition") ($tex+"normal/move_${i}_condition.png") 556 ($y+60) 950 40 "move_${i}_condition" $true $i
            Add-BookSprite ($prefix+"book_move_${i}_description") ($tex+"normal/move_${i}_description.png") 556 ($y+100) 1000 76 "move_${i}_description" $true $i
        }
        Add-BookSprite ($prefix+'book_tip_heading') ($tex+'tip_heading.png') 406 1302 1120 57 '' $true
        Add-BookSprite ($prefix+'book_tip') ($tex+'normal/tip.png') 406 1365 1120 99 'tip' $true
        Add-BookSprite ($prefix+'book_footer') ($tex+'footer.png') 407 959 1180 49
        Add-BookSprite ($prefix+'book_navigation') ($tex+'navigation.png') 762 42 760 51
        Add-BookSprite ($prefix+'book_continue') ($tex+'continue.png') 1426 886 160 35
        Add-BookSprite ($prefix+'book_scroll_track') 'common/white.png' 1604 463 4 385
        Add-BookSprite ($prefix+'book_scroll_thumb') 'ui/settings/settings_slider_knob.png' 1591 463 30 30
        foreach ($s in $items) {
            if ($s.name -eq 'controls_guide_page_1_book_scroll_track') { $s.color = @(0.35,0.66,0.75,0.38) }
            if ($s.name -eq 'controls_guide_page_1_book_scroll_thumb') { $s.color = @(1.0,0.36,0.68,1.0) }
            if ($s.name -eq 'controls_guide_background') { $s.color = @(0.02,0.045,0.08,0.62) }
        }
        $layout.sprites = $items.ToArray()
        $layout | Add-Member -Force -NotePropertyName abilityBook -NotePropertyValue ([ordered]@{
            enabled = $true; viewportTop = 458; viewportBottom = 878; contentBottom = 1466
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
        Add-BasicSprite 'panel' ($tex+'scroll_panel.png') 200 34 1536 1024
        Add-BasicSprite 'portrait' ($tex+'portraits/normal.png') 352 114 380 380
        Add-BasicSprite 'detail_title' ($tex+'basics/title.png') 930 132 720 82
        Add-BasicSprite 'detail_intro' ($tex+'basics/intro.png') 762 277 850 106
        for($i=0; $i -lt 3; ++$i) {
            $y=452+$i*145
            Add-BasicSprite "detail_card_$i" ($tex+'technique_row.png') 395 $y 1180 142
            $items[$items.Count-1].textureCrop=@(0,125,2172,460)
            $glyph=$basics.moves[$i].glyphSize
            Add-BasicSprite "action_$i" $basics.moves[$i].glyph (485-$glyph[0]/2) ($y+71-$glyph[1]/2) $glyph[0] $glyph[1]
            Add-BasicSprite "detail_name_$i" ($tex+"basics/move_${i}_name.png") 585 ($y+10) 900 45
            Add-BasicSprite "detail_condition_$i" ($tex+"basics/move_${i}_condition.png") 585 ($y+48) 950 38
            Add-BasicSprite "detail_description_$i" ($tex+"basics/move_${i}_description.png") 585 ($y+84) 950 40
        }
        Add-BasicSprite 'detail_note' ($tex+'basics/note.png') 406 405 1180 40
        Add-BasicSprite 'detail_footer' ($tex+'basics/footer.png') 407 959 1180 49
        Add-BasicSprite 'detail_navigation' ($tex+'navigation.png') 762 42 760 51
        $layout.sprites=$items.ToArray()
        [IO.File]::WriteAllText($layoutPath,($layout | ConvertTo-Json -Depth 10)+"`n",$taskEncoding)
    }
    Write-Host '基本操作と能力ガイドの日本語テキストを生成しました。'
} finally {
    if (Test-Path -LiteralPath $taskConfig) { Remove-Item -LiteralPath $taskConfig }
}

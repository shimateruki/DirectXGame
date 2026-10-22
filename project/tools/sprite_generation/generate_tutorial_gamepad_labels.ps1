<# チュートリアルのパッド用説明を、既存UIと同じ白文字・濃紺の縁取りで透過生成します。 #>
param([string]$TextTool = '', [string[]]$Names = @(), [switch]$IncludeKeyboard)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $TextTool) { $TextTool = Join-Path $projectRoot '../generated/outputs/Development/TextPngTool.exe' }
if (-not (Test-Path -LiteralPath $TextTool)) { throw "TextPngTool.exe がありません: $TextTool" }
$font = Join-Path $projectRoot 'Resources/font/MPLUS1p-Medium.ttf'
$output = Join-Path $projectRoot 'Resources/sprite/ui/tutorial'
$texconv = Join-Path $projectRoot 'Resources/tools/Texconv.exe'
$utf8 = [Text.UTF8Encoding]::new($false)
$request = Join-Path ([IO.Path]::GetTempPath()) ('tutorial-gamepad-' + [guid]::NewGuid().ToString('N') + '.json')
$labels = [ordered]@{
    move='左スティックで移動'; look='右スティックでカメラ操作'; jump='Aでジャンプ'
    pull='LBを押して狙い、離すとつかむ'; throw='Xで投げる'; absorb='Yでスライムを取り込む'
    release='Bで能力を解除'; controls='VIEWで操作ガイドを開く'
    lock_on='右スティック押し込みでロックオン'
}
# 必要な説明だけを再生成し、既存のメタ情報とGUIDを保持します。
$jobs = @(
    foreach ($entry in $labels.GetEnumerator()) {
        if ($Names.Count -gt 0 -and $entry.Key -notin $Names) { continue }
        @{ FileName=('tutorial_' + $entry.Key + '_gamepad.png'); Text=$entry.Value }
    }
    if ($IncludeKeyboard -and ($Names.Count -eq 0 -or 'pull' -in $Names)) {
        @{ FileName='tutorial_pull.png'; Text='右クリックを押して狙い、離すとつかむ' }
    }
)
try {
    foreach ($job in $jobs) {
        $config = @{
            text=$job.Text; fontPath=$font; fontSize=52; padding=16; autoCanvas=$true
            bold=$true; textColor=@(1.0,0.975,0.925,1.0)
            outline=@{ enabled=$true; width=2.0; color=@(0.08,0.16,0.22,1.0) }
            shadow=@{ enabled=$false }
        }
        [IO.File]::WriteAllText($request, ($config | ConvertTo-Json -Depth 5), $utf8)
        $png = Join-Path $output $job.FileName
        & $TextTool render -config $request -out $png
        if ($LASTEXITCODE -ne 0) { throw "文字画像の生成に失敗: $png" }
        & $texconv -f BC7_UNORM_SRGB -y -m 1 -o $output $png | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "DDS変換に失敗: $png" }
        foreach ($asset in @($png, [IO.Path]::ChangeExtension($png,'.dds'))) {
            if (Test-Path -LiteralPath ($asset + '.meta')) { continue }
            $meta = @{
                assetType='Texture'; guid=[guid]::NewGuid().ToString('N'); importer='TextureImporter'; version=1
                source=$asset.Substring($projectRoot.Length+1).Replace('\','/')
                importSettings=@{ colorSpace='Auto'; generateMipmaps=$false }
            }
            [IO.File]::WriteAllText($asset+'.meta', ($meta | ConvertTo-Json -Depth 4)+"`n", $utf8)
        }
    }
} finally {
    if (Test-Path -LiteralPath $request) { Remove-Item -LiteralPath $request }
}
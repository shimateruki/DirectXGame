param(
    [Parameter(Mandatory = $true)][string]$ProjectRoot,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$DxcPath
)

$ErrorActionPreference = 'Stop'

# 初回タイトル描画までに使うShader Model 6の組だけを配布用に先にコンパイルする。
$shaders = @'
postprocess/PostEffect.VS.hlsl|vs_6_0|main
postprocess/PostEffect.PS.hlsl|ps_6_0|mainCopy
postprocess/PostEffect.PS.hlsl|ps_6_0|mainExtract
postprocess/PostEffect.PS.hlsl|ps_6_0|mainDownsample
postprocess/PostEffect.PS.hlsl|ps_6_0|mainAdd
postprocess/PostEffect.PS.hlsl|ps_6_0|mainComposite
object3d/Skinning.CS.hlsl|cs_6_0|main
object3d/Object3d.VS.hlsl|vs_6_0|main
object3d/Object3d.PS.hlsl|ps_6_0|main
object3d/Shadow.VS.hlsl|vs_6_0|main
materials/Water.VS.hlsl|vs_6_0|main
materials/Water.PS.hlsl|ps_6_0|main
materials/Fire.VS.hlsl|vs_6_0|main
materials/Fire.PS.hlsl|ps_6_0|main
materials/GatePortal.VS.hlsl|vs_6_0|main
materials/GatePortal.PS.hlsl|ps_6_0|main
skybox/Skybox.VS.hlsl|vs_6_0|main
skybox/Skybox.PS.hlsl|ps_6_0|main
sprite/Sprite.VS.hlsl|vs_6_0|main
sprite/Sprite.PS.hlsl|ps_6_0|main
particle/Particle.VS.hlsl|vs_6_0|main
particle/Particle.PS.hlsl|ps_6_0|main
particle/GPUParticleVS.hlsl|vs_6_0|main
particle/GPUParticlePS.hlsl|ps_6_0|main
'@ -split "`r?`n" | Where-Object { $_ }

foreach ($shader in $shaders) {
    $parts = $shader.Split('|')
    $relativePath = "Resources/shader/$($parts[0])"
    $source = Join-Path $ProjectRoot $relativePath
    $output = Join-Path $OutputDirectory "$relativePath.$($parts[1]).$($parts[2]).cso"
    $parent = Split-Path -Parent $output
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    & $DxcPath -E $parts[2] -T $parts[1] -Od -Zpr -I (Join-Path $ProjectRoot 'Resources/shader') -Fo $output $source
    if ($LASTEXITCODE -ne 0) {
        throw "シェーダーの事前コンパイルに失敗: $relativePath ($($parts[1]), $($parts[2]))"
    }
}

// Skybox.PS.hlsl
#include "Skybox.hlsli"

// TextureCubeとしてテクスチャを受け取る
TextureCube<float4> skyboxTexture : register(t0);
SamplerState smp : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    // 3D方向ベクトルを使ってキューブマップから色をサンプリング
    float4 textureColor = skyboxTexture.Sample(smp, input.texcoord);
    
    return textureColor;
}
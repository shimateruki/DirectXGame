// ==========================================================
// GPUParticlePS.hlsl : GPUパーティクル描画用 ピクセルシェーダー
// ==========================================================

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
    float4 projPos : TEXCOORD1; 
};

cbuffer CameraData : register(b0)
{
    row_major matrix viewProj;
    row_major matrix billboardMatrix;
    row_major matrix projection; 
};

Texture2D tex : register(t1);
Texture2D<float> depthTex : register(t2); // 深度バッファ

SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET
{
    float4 texColor = tex.Sample(smp, input.uv);
    float4 finalColor = input.color * texColor;
    if (finalColor.a <= 0.0f)
        discard;

 // =======================================================
    // ★ ソフトパーティクルの計算 (究極進化版)
    // =======================================================
    
    // SV_POSITION (input.pos) の W要素 には、なんと「カメラからそのピクセルまでの実際の距離」がそのまま入っています！
    float linearParticleDepth = input.pos.w;

    // 背景のZ値を線形（実際の距離）に変換
    float bgDepthZ = depthTex.Load(int3(input.pos.xy, 0));
    float m22 = projection._m22;
    float m32 = projection._m32;
    float linearBgDepth = m32 / (bgDepthZ - m22);

    // 距離の差を計算（どれくらい背景に近づいているか）
    float depthDiff = linearBgDepth - linearParticleDepth;

 
    float softFactor = saturate(depthDiff / 5.0f);
    
    finalColor.a *= softFactor;
 

    return finalColor;
}
struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

cbuffer CameraData : register(b0)
{
    row_major matrix viewProj;
    row_major matrix billboardMatrix;
    row_major matrix projection;
    float softParticleFade;
    int blendMode; // ★追加
    float2 screenSize; // ★追加
};

Texture2D tex : register(t1);
Texture2D<float> depthTex : register(t2);
Texture2D grabTex : register(t3); // ★追加: コピーした背景

SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET
{
    // ソフトパーティクルの計算 (前回と同じ)
    float linearParticleDepth = input.pos.w;
    float bgDepthZ = depthTex.Load(int3(input.pos.xy, 0));
    float m22 = projection._m22;
    float m32 = projection._m32;
    float linearBgDepth = m32 / (bgDepthZ - m22);
    float depthDiff = linearBgDepth - linearParticleDepth;
    float softFactor = saturate(depthDiff / softParticleFade);

    float4 finalColor;

    if (blendMode == 2)
    {
        // =======================================================
        // ★ 空間の歪み（色味のブレンド対応版）
        // =======================================================
        float4 texColor = tex.Sample(smp, input.uv);
        
        // 1. 歪みベクトルの計算
        float2 offset = (texColor.rg - 0.5f) * 2.0f;
        offset *= input.color.r * input.color.a * 0.1f * softFactor;
        
        // 2. 歪んだ背景の取得
        float2 screenUV = input.pos.xy / screenSize;
        float3 distortedBg = grabTex.Sample(smp, screenUV + offset).rgb;
        
        // 3. 【新機能】背景にパーティクルの色を「ほんのり」混ぜる！
        // 歪んだ背景 ＋ (エディタで設定した色 × テクスチャの色)
        // input.color.a (透明度) で色の混ざり具合を調整できるようにします
        float3 tintColor = input.color.rgb * texColor.rgb;
        float3 finalRGB = lerp(distortedBg, distortedBg + tintColor, input.color.a * 0.5f);
        
        finalColor = float4(finalRGB, input.color.a * texColor.a * softFactor);
    }
    else
    {
        // 通常の描画 (加算 / 半透明)
        float4 texColor = tex.Sample(smp, input.uv);
        finalColor = input.color * texColor;
        finalColor.a *= softFactor;
    }

    if (finalColor.a <= 0.0f)
        discard;

    return finalColor;
}
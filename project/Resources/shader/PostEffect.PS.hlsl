struct PSInput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float threshold;
    float bloomIntensity;
    float spread;
    int enableToneMapping;
    
    // ★ 追加パラメータ
    float vignetteIntensity;
    float chromaticAberration;
    float filmGrainIntensity;
    float time;
};

float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// 乱数生成関数 (ノイズ用)
float rand(float2 co)
{
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

float4 main(PSInput input) : SV_TARGET
{
    // ========================================================
    // 1. 色収差 (Chromatic Aberration) の適用
    // ========================================================
    float2 center = float2(0.5, 0.5);
    float2 dir = input.uv - center; // 画面中心からの距離ベクトル
    
    // 赤と青のサンプリング位置を、中心から離れるほどズラす
    float r = tex.Sample(smp, input.uv - dir * chromaticAberration).r;
    float g = tex.Sample(smp, input.uv).g; // 緑はそのまま
    float b = tex.Sample(smp, input.uv + dir * chromaticAberration).b;
    float4 baseColor = float4(r, g, b, 1.0);

    // ========================================================
    // 2. ブルームの計算 (変更なし)
    // ========================================================
    float4 bloomColor = float4(0.0, 0.0, 0.0, 0.0);
    float sampleCount = 0.0;
    float dx = 1.0 / 1280.0;
    float dy = 1.0 / 720.0;

    for (int x = -2; x <= 2; x++)
    {
        for (int y = -2; y <= 2; y++)
        {
            float2 offset = float2(x * dx, y * dy) * spread;
            float4 sampleColor = tex.Sample(smp, input.uv + offset);
            float brightness = dot(sampleColor.rgb, float3(0.299, 0.587, 0.114));
            float extract = max(0.0, brightness - threshold);
            bloomColor += sampleColor * extract;
            sampleCount += 1.0;
        }
    }
    bloomColor /= sampleCount;
    float4 finalColor = baseColor + (bloomColor * bloomIntensity);

    // ========================================================
    // 3. トーンマッピング (変更なし)
    // ========================================================
    if (enableToneMapping == 1)
    {
        finalColor.rgb = ACESFilm(finalColor.rgb);
    }
    else if (enableToneMapping == 2)
    {
        float luminance = dot(finalColor.rgb, float3(0.299, 0.587, 0.114));
        float mappedLuminance = ACESFilm(float3(luminance, luminance, luminance)).r;
        finalColor.rgb = finalColor.rgb * (mappedLuminance / (luminance + 0.0001));
    }
    else
    {
        finalColor = saturate(finalColor);
    }

    // ========================================================
    // 4. シネマティックエフェクト (周辺減光 & ノイズ)
    // ========================================================
    // ビネット (Vignette) : 画面の端を暗くする
    float v = 1.0 - dot(dir, dir) * vignetteIntensity;
    finalColor.rgb *= saturate(v);

    // フィルムグレイン (Film Grain) : 画面全体にザラザラ感を足す
    // time変数を足すことで、毎フレーム違うパターンのノイズになる！
    float noise = rand(input.uv + time) * filmGrainIntensity;
    finalColor.rgb -= noise;

    return finalColor;
}
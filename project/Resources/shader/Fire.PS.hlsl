#include "Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
SamplerState smp : register(s0);

float random(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

float noise(float2 st)
{
    float2 i = floor(st);
    float2 f = frac(st);
    float a = random(i);
    float b = random(i + float2(1.0, 0.0));
    float c = random(i + float2(0.0, 1.0));
    float d = random(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(float2 st)
{
    float v = 0.0;
    float a = 0.5;
    float2 shift = float2(100, 100);
    for (int i = 0; i < 3; ++i)
    {
        v += a * noise(st);
        st = st * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

float LinearizeDepth(float z)
{
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    return (nearClip * farClip) / (farClip - z * (farClip - nearClip));
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;

    // 1. ノイズによる炎の形状 (2番目のバージョンに近い設定)
    float2 scrollUV = input.worldPos.xz * 0.4f;
    scrollUV.y = input.worldPos.y * 0.6f - time * 1.3f;
    
    float n1 = fbm(scrollUV);
    float n2 = fbm(scrollUV * 1.5f + float2(time * 0.5f, time * 0.2f));
    float flameValue = n1 * n2 * 2.2f;
    
    // 先端がちぎれるように削るロジックを復活
    float verticalMask = saturate(1.1f - input.uv.y);
    flameValue *= (verticalMask * verticalMask);
    flameValue -= (1.0f - verticalMask) * 0.4f; // 上に行くほど不規則に削る

    if (flameValue < 0.05f) discard;

    // 2. 接地部分のボケ（ソフトエッジ）のみ継続採用
    float bgDepthZ = depthTex.Sample(smp, screenUV).r;
    float bgLinearDepth = LinearizeDepth(bgDepthZ);
    float fireLinearDepth = LinearizeDepth(input.screenPos.z / input.screenPos.w);
    float depthDiff = max(bgLinearDepth - fireLinearDepth, 0.0f);
    float softFactor = saturate(depthDiff / 0.3f);

    // 3. カラーマッピング (赤とオレンジを強調)
    float3 fireDark = float3(0.2f, 0.02f, 0.0f);
    float3 fireRed = color.rgb;
    float3 fireOrange = float3(1.0f, 0.4f, 0.0f);
    float3 fireYellow = float3(1.0f, 0.9f, 0.2f);
    float3 fireWhite = float3(1.5f, 1.5f, 1.2f);

    float3 finalColor = fireDark;
    finalColor = lerp(finalColor, fireRed,    smoothstep(0.1f, 0.35f, flameValue));
    finalColor = lerp(finalColor, fireOrange, smoothstep(0.35f, 0.6f, flameValue));
    finalColor = lerp(finalColor, fireYellow, smoothstep(0.6f, 0.85f, flameValue));
    finalColor = lerp(finalColor, fireWhite,  smoothstep(0.85f, 1.0f, flameValue));

    // 4. 背景歪み (陽炎)
    float2 distort = float2(n1 - 0.5f, n2 - 0.5f) * 0.025f * verticalMask;
    float3 background = grabTex.Sample(smp, screenUV + distort).rgb;

    // 5. 最終合成
    float alpha = saturate(flameValue * 2.0f) * color.a * softFactor;
    return float4(background + finalColor, alpha);
}

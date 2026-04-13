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

float4 main(VSOutput input) : SV_TARGET
{
    // =========================================================
    // 1. マグマで大成功した「真の法線」の計算（シマシマ撲滅）
    // =========================================================
    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 flatNormal = abs(normalize(cross(dx, dy)));

    float2 baseUV;
    if (flatNormal.y > 0.5f)
    {
        baseUV = input.worldPos.xz;
    }
    else if (flatNormal.x > 0.5f)
    {
        baseUV = input.worldPos.zy;
    }
    else
    {
        baseUV = input.worldPos.xy;
    }

    // =========================================================
    // 2. 氷のヒビ・霜（フロスト）のノイズ
    // =========================================================
    // 氷なので時間は使わず、座標だけで静的な模様を作る
    float frost = noise(baseUV * 3.0f);
    float fineFrost = noise(baseUV * 12.0f);
    // 大小2つのノイズを混ぜて、リアルな霜のムラを作る
    float combinedFrost = (frost + fineFrost * 0.5f) / 1.5f;

    // =========================================================
    // 3. 屈折（Refraction）の計算
    // =========================================================
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV.x = screenUV.x * 0.5f + 0.5f;
    screenUV.y = -screenUV.y * 0.5f + 0.5f;

    // 霜のノイズを使って、背景（向こう側の景色）を強く歪ませる
    float2 distortedUV = screenUV + (combinedFrost * 0.06f) - 0.03f;
    float3 refractionColor = grabTex.Sample(smp, distortedUV).rgb;

    // =========================================================
    // 4. ライティングとフレネル（クリスタル感）
    // =========================================================
    // 光の計算には、カクカクのflatNormalではなく元の滑らかな法線を使うと綺麗になる
    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(float3(0.0f, 0.5f, -1.0f));
    float fresnel = pow(1.0f - max(dot(viewDir, normal), 0.0f), 2.0f);

    // =========================================================
    // 5. 最終的な色の合成
    // =========================================================
    // Inspectorの色(color)に霜の白さを混ぜる
    float3 iceColor = lerp(color.rgb, float3(1.0f, 1.0f, 1.0f), combinedFrost * 0.6f);

    float4 finalColor;
    // 歪ませた背景色と、氷の色をアルファ値(color.a)でブレンド
    finalColor.rgb = lerp(refractionColor, iceColor, color.a);

    // 縁（エッジ）をフレネルで白く光らせて、硬い氷の質感を出す
    finalColor.rgb += float3(1.0f, 1.0f, 1.0f) * fresnel * 0.5f;

    // 背景を透かす処理（屈折）は自前でやっているので、出力アルファは1.0でOK
    finalColor.a = 1.0f;

    return finalColor;
}
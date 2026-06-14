#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
SamplerState smp : register(s0);

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float Noise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash12(i);
    float b = Hash12(i + float2(1.0f, 0.0f));
    float c = Hash12(i + float2(0.0f, 1.0f));
    float d = Hash12(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Fbm2(float2 p)
{
    float value = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        value += Noise2D(p) * amp;
        p = p * 2.08f + float2(31.7f, 13.9f);
        amp *= 0.5f;
    }
    return value;
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float density = max(waveHeight, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float scale = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    float mode = effectType;

    float2 p = input.localPos.xy;
    float radial = length(p);
    float height01 = saturate(p.y * 0.5f + 0.5f);
    float angle = atan2(p.y, p.x);
    float2 uv = p * (1.4f + detail * 0.16f) / scale;
    float fogA = Fbm2(uv + float2(time * speed * 0.08f, -time * speed * 0.13f));
    float fogB = Fbm2(uv * 2.15f + float2(-time * speed * 0.18f, time * speed * 0.10f));
    float fog = smoothstep(0.24f, 0.88f, fogA * 0.62f + fogB * 0.48f);

    float energy = 0.0f;
    float spores = 0.0f;

    if (mode < 0.5f)
    {
        float body = 1.0f - smoothstep(0.35f + softness * 0.22f, 1.04f, radial + (fogA - 0.5f) * 0.18f);
        float topFade = smoothstep(0.02f, 0.15f, height01) * (1.0f - smoothstep(0.92f, 1.0f, height01));
        spores = step(0.93f - density * 0.035f, Hash12(floor(uv * (6.0f + detail * 0.35f) + time * speed * 0.8f))) * fog;
        energy = saturate((fog * (0.62f + density * 0.25f) + spores * 0.75f) * body * topFade);
    }
    else if (mode < 1.5f)
    {
        float cloud = 1.0f - smoothstep(0.52f + softness * 0.12f, 1.05f, radial + (fogB - 0.5f) * 0.14f);
        float pockets = smoothstep(0.35f, 0.86f, fogA) * smoothstep(0.25f, 0.90f, fogB);
        spores = step(0.90f - density * 0.045f, Hash12(floor(uv * (9.0f + detail * 0.42f) - time * speed)));
        energy = saturate((pockets * 0.88f + spores * 0.82f) * cloud);
    }
    else
    {
        float sweep = frac(time * speed * 0.20f);
        float ring = 1.0f - smoothstep(0.03f, 0.11f + softness * 0.10f, abs(radial - (0.24f + sweep * 0.62f)));
        float spiral = pow(saturate(0.5f + 0.5f * sin(angle * (5.0f + detail * 0.10f) + radial * 8.0f - time * speed * 2.8f)), 2.5f);
        spores = step(0.92f, Hash12(floor(float2(angle * 10.0f, radial * 12.0f) + time * speed)));
        energy = saturate((ring * (0.75f + spiral) + spores * ring * 0.55f) * (1.0f - smoothstep(0.92f, 1.12f, radial)));
    }

    float2 drift = float2(fogA - 0.5f, fogB - 0.5f) * (0.014f + density * 0.006f) * energy;
    float3 sceneColor = grabTex.SampleLevel(smp, screenUV, 0).rgb;
    float3 warped = grabTex.SampleLevel(smp, saturate(screenUV + drift), 0).rgb;

    float poisonMode = saturate(mode * 0.5f);
    float3 greenPoison = float3(0.32f, 1.05f, 0.18f);
    float3 purpleSpore = float3(0.72f, 0.22f, 1.0f);
    float3 poisonTint = lerp(greenPoison, purpleSpore, poisonMode);
    poisonTint = lerp(poisonTint, saturate(color.rgb), 0.35f);

    float3 finalColor = lerp(sceneColor, warped, saturate(energy * 0.62f));
    finalColor = lerp(finalColor, poisonTint * (0.75f + spores * 1.35f) * intensity, saturate(energy * 0.76f));
    finalColor += poisonTint * spores * intensity * 0.28f;

    float alpha = saturate(color.a * energy * (0.20f + density * 0.13f + intensity * 0.04f));
    if (alpha < 0.008f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}

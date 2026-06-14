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

float Bolt(float2 p, float seed, float speed)
{
    float path = sin(p.y * 7.0f + seed * 6.28f + time * speed * 5.0f) * 0.16f;
    path += sin(p.y * 17.0f - time * speed * 9.0f + seed) * 0.045f;
    return 1.0f - smoothstep(0.018f, 0.075f, abs(p.x - path));
}

float4 main(VSOutput input) : SV_TARGET
{
    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float width = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    float mode = effectType;

    float2 p = input.localPos.xy;
    float radial = length(p);
    float angle = atan2(p.y, p.x);
    float y01 = saturate(p.y * 0.5f + 0.5f);

    float energy = 0.0f;
    float hot = 0.0f;

    if (mode < 0.5f)
    {
        float ringA = 1.0f - smoothstep(0.015f, 0.07f + softness * 0.12f, abs(radial - 0.40f));
        float ringB = 1.0f - smoothstep(0.015f, 0.07f + softness * 0.12f, abs(radial - 0.72f));
        float scan = pow(saturate(0.5f + 0.5f * sin(angle * (5.0f + detail * 0.12f) - time * speed * 4.0f)), 3.5f);
        energy = saturate((ringA * 0.55f + ringB * 0.40f) * (0.30f + scan * 0.45f));
        hot = scan * saturate(ringA + ringB);
    }
    else if (mode < 1.5f)
    {
        float cageV = 1.0f - smoothstep(0.018f, 0.08f + width * 0.01f, abs(sin((p.x + 0.04f * sin(p.y * 7.0f)) * (10.0f + detail * 0.28f))));
        float cageH = 1.0f - smoothstep(0.018f, 0.07f + softness * 0.08f, abs(sin((p.y - time * speed * 0.25f) * (8.0f + detail * 0.20f))));
        float ellipse = 1.0f - smoothstep(0.82f, 1.08f, radial);
        float spark = step(0.965f, Hash12(floor(p * 16.0f + time * speed * 4.0f)));
        energy = saturate((cageV * 0.28f + cageH * 0.22f + spark * 0.55f) * ellipse);
        hot = saturate(cageV + spark);
    }
    else
    {
        float orb = 1.0f - smoothstep(0.55f + softness * 0.10f, 1.02f, radial);
        float radialRing = 1.0f - smoothstep(0.02f, 0.09f + softness * 0.08f, abs(radial - (0.28f + frac(time * speed * 0.45f) * 0.52f)));
        float lightning = Bolt(float2(p.x, p.y), Hash12(floor(p * 3.0f)), speed) * orb;
        energy = saturate((orb * 0.06f + radialRing * 0.48f + lightning * 0.45f));
        hot = saturate(radialRing + lightning);
    }

    float fresnelBoost = pow(saturate(1.0f - radial), 0.6f) * 0.08f;
    energy = saturate(energy + fresnelBoost);
    energy *= 0.62f;

    float3 bindTint = lerp(float3(1.0f, 0.86f, 0.18f), saturate(color.rgb), 0.55f);
    float3 core = float3(2.8f, 2.45f, 1.15f);
    float3 finalColor = (bindTint * energy * 1.05f + core * hot * 0.42f) * intensity;

    float alpha = saturate(color.a * energy * (0.24f + intensity * 0.04f));
    if (alpha < 0.01f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}

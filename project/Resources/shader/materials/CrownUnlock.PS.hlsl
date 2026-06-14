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

float StarRay(float angle, float count, float phase)
{
    float ray = 0.5f + 0.5f * sin(angle * count + phase);
    return pow(saturate(ray), 8.0f);
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float scale = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    float mode = effectType;

    float2 p = input.localPos.xy / scale;
    float radial = length(p);
    float angle = atan2(p.y, p.x);
    float phase = time * speed * 2.2f;

    float energy = 0.0f;
    float core = 0.0f;

    if (mode < 0.5f)
    {
        float ringA = 1.0f - smoothstep(0.015f, 0.055f + softness * 0.10f, abs(radial - 0.38f));
        float ringB = 1.0f - smoothstep(0.018f, 0.065f + softness * 0.10f, abs(radial - 0.72f));
        float glyph = step(0.78f, Hash12(floor(float2(angle * 7.0f, radial * 9.0f) + time * speed * 0.3f)));
        float rays = StarRay(angle, 6.0f + detail * 0.10f, -phase);
        energy = saturate(ringA * 1.1f + ringB * (0.55f + rays) + glyph * ringB * 0.55f);
        core = ringA + glyph * 0.5f;
    }
    else if (mode < 1.5f)
    {
        float crownBase = 1.0f - smoothstep(0.02f, 0.08f + softness * 0.08f, abs(p.y + 0.22f));
        crownBase *= 1.0f - smoothstep(0.58f, 0.86f, abs(p.x));
        float peak1 = 1.0f - smoothstep(0.04f, 0.16f + softness * 0.08f, abs(p.y - (0.10f + abs(p.x) * 0.72f)));
        peak1 *= 1.0f - smoothstep(0.05f, 0.70f, abs(p.x));
        float peak2 = 1.0f - smoothstep(0.03f, 0.12f + softness * 0.07f, abs(p.y - (0.52f - abs(p.x) * 0.50f)));
        peak2 *= smoothstep(0.18f, 0.38f, abs(p.x)) * (1.0f - smoothstep(0.48f, 0.78f, abs(p.x)));
        float burst = StarRay(angle, 10.0f + detail * 0.12f, phase) * (1.0f - smoothstep(0.22f, 1.05f, radial));
        energy = saturate(crownBase + peak1 * 0.9f + peak2 * 0.85f + burst * 0.55f);
        core = saturate(crownBase + peak1 + peak2);
    }
    else
    {
        float sweep = frac(time * speed * 0.25f);
        float portal = 1.0f - smoothstep(0.020f, 0.080f + softness * 0.12f, abs(radial - (0.30f + sweep * 0.45f)));
        float outer = 1.0f - smoothstep(0.018f, 0.075f + softness * 0.10f, abs(radial - 0.78f));
        float beams = StarRay(angle + radial * 2.0f, 12.0f + detail * 0.10f, -phase * 1.6f);
        energy = saturate((portal * 1.1f + outer * (0.45f + beams)) * (1.0f - smoothstep(0.98f, 1.18f, radial)));
        core = saturate(portal + outer * beams);
    }

    float3 gold = lerp(float3(1.0f, 0.68f, 0.12f), saturate(color.rgb), 0.32f);
    float3 whiteGold = float3(2.4f, 2.1f, 1.05f);
    float3 magic = (gold * energy * 2.1f + whiteGold * core * 1.1f) * intensity;
    float3 finalColor = magic * (0.75f + core * 0.35f);

    float alpha = saturate(color.a * energy * (0.45f + intensity * 0.07f));
    if (alpha < 0.01f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}

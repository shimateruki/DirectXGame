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
    for (int i = 0; i < 4; ++i)
    {
        value += Noise2D(p) * amp;
        p = p * 2.04f + float2(21.1f, 9.7f);
        amp *= 0.5f;
    }
    return value;
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float wobble = max(waveHeight, 0.0f);
    float detail = max(waveFrequency, 0.1f);
    float scale = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    float mode = effectType;

    float2 p = input.localPos.xy;
    float radial = length(p);
    float height01 = saturate(p.y * 0.5f + 0.5f);
    float angle = atan2(p.y, p.x);
    float windNoise = Fbm2(p * (2.0f + detail * 0.16f) / scale + float2(time * speed * 0.10f, -time * speed * 0.36f));

    float energy = 0.0f;
    float curl = 0.0f;

    if (mode < 0.5f)
    {
        float sway = sin(p.y * 2.4f + time * speed * 1.4f) * (0.08f + wobble * 0.018f);
        float column = 1.0f - smoothstep(0.34f + softness * 0.12f, 0.88f, abs(p.x + sway) + (windNoise - 0.5f) * 0.10f);
        float vertical = frac((height01 + time * speed * 0.24f + windNoise * 0.10f) * (5.0f + detail * 0.20f));
        float streak = 1.0f - smoothstep(0.05f, 0.24f + softness * 0.08f, vertical);
        float baseLift = 1.0f - smoothstep(0.02f, 0.24f, height01);
        float fade = smoothstep(0.00f, 0.14f, height01) * (1.0f - smoothstep(0.90f, 1.0f, height01));
        energy = saturate((column * 0.22f + streak * column * 0.92f + baseLift * column * 0.35f) * fade);
        curl = streak;
    }
    else if (mode < 1.5f)
    {
        float sweep = frac(time * speed * 0.28f);
        float ring = 1.0f - smoothstep(0.018f, 0.07f + softness * 0.10f, abs(radial - (0.22f + sweep * 0.72f)));
        float spiral = pow(saturate(0.5f + 0.5f * sin(atan2(p.y, p.x) * (3.0f + detail * 0.12f) + radial * 13.0f - time * speed * 3.5f)), 3.0f);
        float disk = 1.0f - smoothstep(0.88f, 1.08f, radial);
        energy = saturate((ring * 1.2f + spiral * 0.35f) * disk);
        curl = spiral;
    }
    else
    {
        float slash = abs(p.y + sin(p.x * 4.2f - time * speed * 2.3f) * (0.10f + wobble * 0.02f));
        float body = 1.0f - smoothstep(0.04f, 0.20f + softness * 0.12f, slash);
        float lengthFade = 1.0f - smoothstep(0.55f, 1.08f, abs(p.x));
        float streaks = pow(saturate(0.5f + 0.5f * sin(p.x * (18.0f + detail) - time * speed * 6.0f + windNoise * 5.0f)), 2.2f);
        energy = saturate(body * lengthFade * (0.45f + streaks * 0.75f));
        curl = streaks;
    }

    float2 distort = normalize(p + 0.0001f) * energy * (0.014f + wobble * 0.006f);
    float3 sceneColor = grabTex.SampleLevel(smp, screenUV, 0).rgb;
    float3 warped = grabTex.SampleLevel(smp, saturate(screenUV + distort), 0).rgb;

    float3 windTint = lerp(float3(0.58f, 0.96f, 1.35f), saturate(color.rgb), 0.45f);
    float3 finalColor = lerp(sceneColor, warped, saturate(energy * 0.78f));
    finalColor += windTint * energy * intensity * (0.70f + curl * 0.65f);

    float alpha = saturate(color.a * energy * (0.28f + intensity * 0.09f));
    if (alpha < 0.008f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}

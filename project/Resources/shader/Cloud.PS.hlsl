#include "Water.hlsli"

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
    float amp = 0.55f;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        value += Noise2D(p) * amp;
        p = p * 2.02f + float2(13.7f, 29.1f);
        amp *= 0.52f;
    }
    return value;
}

float CloudLobe(float2 p, float2 center, float radius, float stretch)
{
    float2 q = p - center;
    q.x /= max(stretch, 0.05f);
    return 1.0f - smoothstep(radius * 0.35f, radius, length(q));
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float density = max(waveHeight, 0.0f);
    float detail = max(waveFrequency, 0.1f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    float mode = effectType;

    float2 p = input.localPos.xy;
    float t = time * speed;
    float2 drift = float2(t * 0.055f + flowSpeedX * time * 0.01f, -t * 0.025f + flowSpeedY * time * 0.01f);
    float cloudNoise = Fbm2(p * (1.8f + detail * 0.15f) + drift);
    float fineNoise = Fbm2(p * (4.0f + detail * 0.28f) - drift * 1.6f);

    float body = 0.0f;
    if (mode < 0.5f)
    {
        body += CloudLobe(p, float2(-0.24f, -0.02f), 0.68f, 1.25f);
        body += CloudLobe(p, float2(0.20f, 0.06f), 0.62f, 1.10f);
        body += CloudLobe(p, float2(0.02f, 0.24f), 0.52f, 0.90f);
    }
    else if (mode < 1.5f)
    {
        float band = 1.0f - smoothstep(0.25f + softness * 0.15f, 0.92f, abs(p.y + sin(p.x * 2.6f + t) * 0.10f));
        float lengthFade = 1.0f - smoothstep(0.62f, 1.08f, abs(p.x));
        body = band * lengthFade;
    }
    else
    {
        float lowMist = 1.0f - smoothstep(-0.82f, 0.18f + softness * 0.25f, p.y);
        float width = 1.0f - smoothstep(0.65f, 1.12f, abs(p.x + sin(t + p.y * 2.0f) * 0.08f));
        body = lowMist * width;
    }

    body = saturate(body * (0.72f + cloudNoise * 0.55f));
    float edge = 1.0f - smoothstep(0.34f + softness * 0.18f, 0.88f + softness * 0.15f, length(p));
    if (mode >= 0.5f && mode < 1.5f)
    {
        edge = 1.0f - smoothstep(0.55f, 1.12f, abs(p.x));
    }
    body *= edge;

    float holes = smoothstep(0.16f, 0.72f, cloudNoise + fineNoise * 0.35f);
    float alpha = saturate(color.a * body * holes * (0.18f + density * 0.24f) * intensity);
    float rim = pow(saturate(1.0f - length(p) * 0.72f), 1.6f) * 0.25f;

    float3 sceneColor = grabTex.SampleLevel(smp, screenUV, 0).rgb;
    float2 distort = normalize(p + 0.0001f) * alpha * 0.012f;
    float3 warped = grabTex.SampleLevel(smp, saturate(screenUV + distort), 0).rgb;
    float3 cloudTint = lerp(float3(0.72f, 0.82f, 0.92f), saturate(color.rgb), 0.35f);
    float3 highlight = float3(1.0f, 0.97f, 0.88f) * (0.22f + rim + fineNoise * 0.12f);
    float3 finalColor = lerp(warped, cloudTint + highlight, saturate(alpha * 0.82f + rim * 0.35f));

    if (alpha < 0.008f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}

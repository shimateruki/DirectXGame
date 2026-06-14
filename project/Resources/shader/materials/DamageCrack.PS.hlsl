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
        p = p * 2.08f + float2(23.7f, 11.1f);
        amp *= 0.52f;
    }
    return value;
}

float2 SurfaceUV(float3 worldPos, float3 normal)
{
    float3 n = abs(normalize(normal));
    n = max(n, float3(0.001f, 0.001f, 0.001f));
    n /= n.x + n.y + n.z;
    float2 uvX = worldPos.zy;
    float2 uvY = worldPos.xz;
    float2 uvZ = worldPos.xy;
    return uvX * n.x + uvY * n.y + uvZ * n.z;
}

float CrackLines(float2 uv, float detail, float scale, float bias)
{
    uv *= max(scale, 0.05f);
    float cells = 4.0f + detail * 0.42f;
    float2 g = frac(uv * cells) - 0.5f;
    float2 id = floor(uv * cells);

    float seed = Hash12(id);
    float angle = seed * 6.2831853f;
    float2 dir = float2(cos(angle), sin(angle));
    float primary = abs(dot(g, float2(-dir.y, dir.x)) + (seed - 0.5f) * 0.10f);
    float along = abs(dot(g, dir));
    float branchNoise = Fbm2(uv * (1.8f + detail * 0.07f) + seed * 9.0f);
    float jagged = primary + (branchNoise - 0.5f) * 0.075f;

    float mainLine = 1.0f - smoothstep(0.010f, 0.035f + bias * 0.055f, jagged);
    float branchA = 1.0f - smoothstep(0.008f, 0.026f + bias * 0.045f, abs(g.y + g.x * (0.42f + seed * 0.35f)));
    branchA *= 1.0f - smoothstep(0.04f, 0.30f, along);
    float broken = step(0.34f, branchNoise) * step(seed, 0.86f);

    return saturate((mainLine + branchA * 0.72f) * broken);
}

float RadialWeakPoint(float2 uv, float detail, float softness)
{
    float2 p = frac(uv * 0.35f) - 0.5f;
    float r = length(p);
    float a = atan2(p.y, p.x);
    float rays = pow(saturate(0.5f + 0.5f * sin(a * (7.0f + detail * 0.14f) + Fbm2(uv * 2.4f) * 5.0f)), 7.0f);
    float ring = 1.0f - smoothstep(0.012f, 0.055f + softness * 0.055f, abs(r - 0.22f));
    float spokes = rays * (1.0f - smoothstep(0.08f, 0.48f, r));
    return saturate(ring * 0.8f + spokes);
}

float4 main(VSOutput input) : SV_TARGET
{
    float detail = max(waveFrequency, 0.1f);
    float scale = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    float mode = effectType;

    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(cameraWorldPosition - input.worldPos);
    float facing = saturate(dot(normal, viewDir) * 0.75f + 0.25f);

    float2 uv = SurfaceUV(input.worldPos, normal);
    float crack = CrackLines(uv, detail, scale, softness);
    float extra = CrackLines(uv * 1.73f + float2(4.1f, 8.7f), detail * 0.8f, scale * 0.68f, softness) * 0.55f;
    float mask = saturate(crack + extra);

    float3 crackColor = float3(0.045f, 0.035f, 0.030f);
    float3 edgeColor = float3(0.38f, 0.30f, 0.20f);
    float alphaBase = 0.62f;

    if (mode > 0.5f && mode < 1.5f)
    {
        float glassStar = RadialWeakPoint(uv * 1.15f, detail, softness);
        mask = saturate(mask * 0.70f + glassStar);
        crackColor = float3(0.80f, 0.95f, 1.0f);
        edgeColor = float3(0.25f, 0.58f, 0.75f);
        alphaBase = 0.48f;
    }
    else if (mode >= 1.5f)
    {
        float weakPoint = RadialWeakPoint(uv * 1.25f + float2(0.17f, 0.31f), detail, softness);
        mask = saturate(mask * 0.75f + weakPoint * 0.95f);
        crackColor = float3(1.0f, 0.24f, 0.06f);
        edgeColor = float3(1.8f, 0.72f, 0.16f);
        alphaBase = 0.55f;
    }

    float glowEdge = smoothstep(0.18f, 0.92f, mask) * (1.0f - smoothstep(0.72f, 1.0f, mask));
    float pulse = 0.92f + 0.08f * sin(time * max(waveSpeed, 0.05f) * 2.5f);
    float alpha = saturate(color.a * mask * alphaBase * intensity * facing * pulse);

    if (alpha < 0.01f)
    {
        discard;
    }

    float3 finalColor = lerp(crackColor, edgeColor, glowEdge) * (0.85f + intensity * 0.18f);
    return float4(finalColor, alpha);
}

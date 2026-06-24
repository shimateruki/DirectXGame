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

float PointSpark(float2 p, float2 center, float radius)
{
    return 1.0f - smoothstep(radius, radius * 2.8f, length(p - center));
}

float ElectricSegment(float2 p, float2 start, float2 end, float seed, float lineWidth, float speed)
{
    float2 axis = end - start;
    float lenSq = max(dot(axis, axis), 0.0001f);
    float t = saturate(dot(p - start, axis) / lenSq);
    float2 tangent = normalize(axis);
    float2 normal = float2(-tangent.y, tangent.x);

    float cell = floor(t * 9.0f + seed * 2.0f);
    float jitter = (Hash12(float2(cell, seed * 19.13f)) - 0.5f) * 0.065f;
    jitter += sin(t * 37.0f + seed * 8.0f + time * speed * 13.0f) * 0.018f;
    jitter += sin(t * 81.0f - seed * 5.0f - time * speed * 17.0f) * 0.012f;

    float2 closest = start + axis * t + normal * jitter;
    float d = length(p - closest);
    float core = 1.0f - smoothstep(lineWidth, lineWidth * 3.2f, d);
    float bodyFade = smoothstep(0.02f, 0.12f, t) * (1.0f - smoothstep(0.88f, 1.0f, t));
    float flicker = 0.72f + 0.28f * sin(time * speed * 41.0f + seed * 11.0f);
    return core * bodyFade * flicker;
}

float ElectricBranch(float2 p, float2 start, float2 end, float seed, float lineWidth, float speed)
{
    float2 axis = end - start;
    float2 tangent = normalize(axis);
    float2 normal = float2(-tangent.y, tangent.x);

    float mainBolt = ElectricSegment(p, start, end, seed, lineWidth, speed);
    float2 jointA = lerp(start, end, 0.34f);
    float2 jointB = lerp(start, end, 0.64f);
    float branchA = ElectricSegment(p, jointA, jointA + normal * 0.20f + tangent * 0.11f, seed + 2.7f, lineWidth * 0.72f, speed);
    float branchB = ElectricSegment(p, jointB, jointB - normal * 0.18f + tangent * 0.12f, seed + 5.1f, lineWidth * 0.66f, speed);
    return saturate(mainBolt + branchA * 0.72f + branchB * 0.64f);
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
    float radialMask = 1.0f - smoothstep(0.92f + softness * 0.08f, 1.12f + softness * 0.10f, radial);
    float lineWidth = 0.009f + saturate(width * 0.45f) * 0.015f + softness * 0.006f;

    if (mode < 0.5f)
    {
        float arcGate = pow(saturate(0.5f + 0.5f * sin(angle * (5.0f + detail * 0.12f) - time * speed * 8.0f)), 8.0f);
        float orbit = 1.0f - smoothstep(lineWidth, lineWidth * 3.4f, abs(radial - (0.52f + sin(time * speed * 2.3f) * 0.035f)));
        float boltA = ElectricSegment(p, float2(-0.34f, -0.12f), float2(0.22f, 0.36f), 1.0f, lineWidth, speed);
        float boltB = ElectricSegment(p, float2(0.18f, -0.32f), float2(-0.26f, 0.16f), 3.0f, lineWidth * 0.88f, speed);
        float spark = PointSpark(p, float2(sin(time * speed * 3.1f) * 0.36f, cos(time * speed * 2.7f) * 0.28f), lineWidth * 1.35f);
        energy = saturate((orbit * arcGate * 0.30f + boltA * 0.74f + boltB * 0.58f + spark * 0.34f) * radialMask);
        hot = saturate(boltA + boltB + spark * 0.65f);
    }
    else if (mode < 1.5f)
    {
        float boltA = ElectricBranch(p, float2(-0.58f, -0.52f), float2(0.42f, 0.58f), 7.0f, lineWidth, speed);
        float boltB = ElectricBranch(p, float2(0.54f, -0.48f), float2(-0.36f, 0.46f), 13.0f, lineWidth * 0.92f, speed);
        float boltC = ElectricSegment(p, float2(-0.64f, 0.10f), float2(0.62f, -0.18f), 19.0f, lineWidth * 0.80f, speed);
        float sparkSeed = Hash12(floor(p * 13.0f + time * speed * 7.0f));
        float spark = step(0.975f, sparkSeed) * PointSpark(frac(p * 5.0f) - 0.5f, float2(0.0f, 0.0f), lineWidth * 2.0f);
        energy = saturate((boltA * 0.78f + boltB * 0.72f + boltC * 0.46f + spark * 0.34f) * radialMask);
        hot = saturate(boltA + boltB + boltC * 0.8f + spark);
    }
    else
    {
        float aura = (1.0f - smoothstep(0.62f, 1.05f, radial)) * (0.10f + 0.05f * sin(time * speed * 8.0f + angle * 4.0f));
        float arcGate = pow(saturate(0.5f + 0.5f * sin(angle * 4.0f + time * speed * 7.0f)), 6.5f);
        float rim = (1.0f - smoothstep(lineWidth, lineWidth * 3.6f, abs(radial - (0.72f + sin(time * speed * 2.0f) * 0.045f)))) * arcGate;
        float boltA = ElectricBranch(p, float2(-0.46f, -0.30f), float2(0.44f, 0.32f), 23.0f, lineWidth, speed);
        float boltB = ElectricSegment(p, float2(0.18f, -0.56f), float2(-0.18f, 0.58f), 29.0f, lineWidth * 0.82f, speed);
        energy = saturate((aura * 0.22f + rim * 0.28f + boltA * 0.68f + boltB * 0.46f) * radialMask);
        hot = saturate(rim * 0.42f + boltA + boltB);
    }

    float centerGlow = pow(saturate(1.0f - radial), 1.8f) * 0.035f;
    energy = saturate(energy + centerGlow);

    float pulse = 0.82f + 0.18f * sin(time * speed * 38.0f + angle * 5.0f);
    float3 bindTint = lerp(float3(1.0f, 0.78f, 0.06f), saturate(color.rgb), 0.28f);
    float3 core = float3(3.4f, 2.85f, 1.25f);
    float3 finalColor = (bindTint * energy * 1.45f + core * hot * 0.72f) * intensity * pulse;

    float alpha = saturate(color.a * (energy * 0.48f + hot * 0.34f) * (0.72f + intensity * 0.10f));
    if (alpha < 0.01f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}

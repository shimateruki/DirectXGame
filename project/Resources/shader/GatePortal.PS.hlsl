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
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        value += Noise2D(p) * amp;
        p = p * 2.03f + float2(19.7f, 7.3f);
        amp *= 0.52f;
    }
    return value;
}

float RingMask(float value, float center, float width)
{
    float halfWidth = max(width, 0.001f) * 0.5f;
    return 1.0f - smoothstep(halfWidth * 0.25f, halfWidth, abs(value - center));
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float depthPower = max(waveHeight, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    float mode = effectType;
    float t = time * speed;

    float2 p = input.localPos.xy;
    p.x *= 0.72f;
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float portal = 1.0f - smoothstep(0.82f + softness * 0.07f, 1.04f + softness * 0.12f, radius);
    float innerFade = smoothstep(0.03f, 0.38f, radius);
    float edge = RingMask(radius, 0.84f, 0.18f + softness * 0.20f);
    float hotEdge = RingMask(radius, 0.73f, 0.08f + softness * 0.08f);

    float2 swirlUV = float2(
        angle * 0.42f + radius * (1.8f + effectScale * 1.4f) - t * 0.18f,
        radius * (3.2f + effectScale * 2.2f) + t * 0.10f
    );
    float coarse = Fbm2(swirlUV * (1.2f + detail * 0.09f));
    float fine = Fbm2(swirlUV * (3.0f + detail * 0.18f) + float2(t * 0.4f, -t * 0.22f));
    float arms = sin(angle * (3.0f + floor(detail * 0.12f)) + radius * (7.0f + detail * 0.45f) - t * 2.2f + coarse * 3.8f);
    arms = pow(saturate(arms * 0.5f + 0.5f), 2.2f);
    float inward = pow(saturate(1.0f - radius), 1.6f + depthPower * 0.35f);

    float spark = smoothstep(0.78f, 0.98f, fine + arms * 0.36f) * portal;
    float flow = saturate(arms * 0.58f + coarse * 0.42f);
    float coreDark = pow(saturate(1.0f - radius * 1.24f), 2.0f + depthPower);

    float3 warmA = float3(1.0f, 0.42f, 0.08f);
    float3 warmB = float3(1.0f, 0.84f, 0.24f);
    float3 blueA = float3(0.28f, 0.30f, 1.0f);
    float3 blueB = float3(0.74f, 0.56f, 1.0f);
    float3 gateA = (mode > 1.5f) ? blueA : warmA;
    float3 gateB = (mode > 1.5f) ? blueB : warmB;
    if (mode > 0.5f && mode <= 1.5f)
    {
        gateA = float3(1.0f, 0.55f, 0.13f);
        gateB = float3(1.0f, 0.92f, 0.42f);
    }

    float2 tangent = normalize(float2(-p.y, p.x) + 0.0001f);
    float2 radial = normalize(p + 0.0001f);
    float distortStrength = portal * (0.006f + flow * 0.018f + edge * 0.014f) * intensity;
    float2 distortUV = screenUV + (tangent * arms + radial * (coarse - 0.5f)) * distortStrength;
    float3 scene = grabTex.SampleLevel(smp, saturate(distortUV), 0).rgb;

    float3 vortex = lerp(gateA, gateB, saturate(flow + inward * 0.35f));
    vortex += gateB * (spark * 0.65f + edge * 0.75f + hotEdge * 0.45f) * intensity;
    vortex = lerp(vortex, float3(0.12f, 0.055f, 0.035f), coreDark * 0.72f);

    float sealBand = 0.0f;
    if (mode > 1.5f)
    {
        float radialStripe = RingMask(frac(angle * 1.9f + t * 0.2f), 0.5f, 0.05f);
        sealBand = radialStripe * RingMask(radius, 0.54f, 0.07f) * portal;
        vortex += float3(0.72f, 0.78f, 1.0f) * sealBand * intensity;
    }

    float alpha = portal * (0.46f + flow * 0.26f + edge * 0.32f + spark * 0.18f) * color.a;
    alpha = saturate(alpha * (0.78f + intensity * 0.18f) * innerFade);

    float3 finalColor = lerp(scene * 0.42f, vortex * saturate(color.rgb + 0.25f), saturate(alpha + edge * 0.35f));
    finalColor += gateB * (edge + spark + sealBand) * 0.35f * intensity;

    if (alpha < 0.01f)
    {
        discard;
    }

    return float4(finalColor, alpha);
}

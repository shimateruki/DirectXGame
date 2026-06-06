#include "Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
SamplerState smp : register(s0);

static const float kNearClip = 0.1f;
static const float kFarClip = 1000.0f;

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
        p = p * 2.07f + float2(23.1f, 11.7f);
        amp *= 0.5f;
    }

    return value;
}

float LinearizeDepth(float z)
{
    z = saturate(z);
    return (kNearClip * kFarClip) / max(kFarClip - z * (kFarClip - kNearClip), 0.0001f);
}

float2 GetPlaneUV(float3 localPos, float3 normal)
{
    float3 n = abs(normalize(normal));
    if (n.y >= n.x && n.y >= n.z)
    {
        return localPos.xz;
    }
    if (n.x >= n.z)
    {
        return localPos.zy;
    }
    return localPos.xy;
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float radiusScale = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);

    float2 planeUV = GetPlaneUV(input.localPos, input.normal);
    float radial = length(planeUV) / radiusScale;
    float angle = atan2(planeUV.y, planeUV.x);

    float progress = frac(time * speed * 0.32f + effectType * 0.173f);
    float ringWidth = lerp(0.025f, 0.16f, softness);
    float primary = 1.0f - smoothstep(ringWidth, ringWidth * 2.25f, abs(radial - progress));
    primary *= smoothstep(0.03f, 0.18f, progress) * (1.0f - smoothstep(0.82f, 1.08f, progress));

    float secondaryCenter = saturate(progress - 0.23f);
    float secondary = 1.0f - smoothstep(ringWidth * 1.2f, ringWidth * 3.0f, abs(radial - secondaryCenter));
    secondary *= smoothstep(0.28f, 0.45f, progress) * 0.55f;

    float2 crackUV = planeUV * (10.0f + detail * 2.0f);
    float crackNoise = Fbm2(crackUV + float2(time * 0.12f, -time * 0.09f));
    float spoke = pow(saturate(0.5f + 0.5f * sin(angle * (11.0f + detail) + crackNoise * 6.0f)), 4.0f);
    float dust = smoothstep(0.36f, 0.92f, Fbm2(planeUV * (3.5f + detail * 0.7f) - time * 0.18f));

    float energy = saturate(primary * (0.70f + spoke * 0.55f) + secondary * 0.6f + dust * primary * 0.24f);
    float fadeOuter = 1.0f - smoothstep(0.96f, 1.18f, radial);
    energy *= fadeOuter;

    float bgDepth = depthTex.SampleLevel(smp, screenUV, 0).r;
    float waveDepth = input.screenPos.z / input.screenPos.w;
    float depthDiff = LinearizeDepth(bgDepth) - LinearizeDepth(waveDepth);
    float depthFade = (bgDepth >= 0.999f) ? 1.0f : saturate(depthDiff / 0.9f + 0.35f);

    float2 push = normalize(planeUV + 0.0001f) * energy * (0.018f + softness * 0.018f);
    float3 sceneColor = grabTex.SampleLevel(smp, screenUV, 0).rgb;
    float3 warpedScene = grabTex.SampleLevel(smp, saturate(screenUV + push), 0).rgb;

    float3 waveTint = lerp(float3(0.30f, 0.82f, 1.25f), saturate(color.rgb), 0.50f);
    float3 brightCore = float3(1.55f, 1.85f, 2.15f);
    float3 shockColor = lerp(waveTint, brightCore, primary * 0.55f) * energy * intensity;
    shockColor += float3(0.20f, 0.36f, 0.55f) * dust * primary * intensity;

    float alpha = saturate(energy * color.a * depthFade);
    if (alpha < 0.01f)
    {
        discard;
    }

    float3 finalColor = lerp(sceneColor, warpedScene, saturate(energy * 0.85f));
    finalColor = lerp(finalColor, shockColor + finalColor * 0.35f, saturate(alpha * 0.88f));

    return float4(finalColor, alpha);
}

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
        p = p * 2.03f + float2(19.1f, 37.7f);
        amp *= 0.5f;
    }

    return value;
}

float LinearizeDepth(float z)
{
    z = saturate(z);
    return (kNearClip * kFarClip) / max(kFarClip - z * (kFarClip - kNearClip), 0.0001f);
}

float2 TriplanarGelUV(float3 worldPos, float3 normal)
{
    float3 n = abs(normalize(normal));
    n = max(n, float3(0.001f, 0.001f, 0.001f));
    n /= (n.x + n.y + n.z);

    float2 uvX = worldPos.zy;
    float2 uvY = worldPos.xz;
    float2 uvZ = worldPos.xy;
    return uvX * n.x + uvY * n.y + uvZ * n.z;
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);

    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float refractionScale = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);

    float3 normal = normalize(input.normal);
    float3 viewDir = normalize(cameraWorldPosition - input.worldPos);
    float fresnel = pow(1.0f - saturate(dot(normal, viewDir)), lerp(1.25f, 4.0f, softness));

    float2 gelUV = TriplanarGelUV(input.worldPos, normal) * (1.2f + detail * 0.32f);
    float flowA = Fbm2(gelUV + float2(time * speed * 0.12f, -time * speed * 0.18f));
    float flowB = Fbm2(gelUV * 2.15f + float2(-time * speed * 0.24f, time * speed * 0.16f));
    float membranes = smoothstep(0.46f, 0.74f, abs(flowA - flowB) * 1.75f);
    float bubbleField = Fbm2(gelUV * 4.2f + float2(time * 0.19f, -time * 0.11f));
    float bubbles = smoothstep(0.78f, 0.95f, bubbleField + fresnel * 0.12f);

    float2 bendNoise;
    bendNoise.x = flowA * 2.0f - 1.0f;
    bendNoise.y = flowB * 2.0f - 1.0f;
    float2 distortion = (normal.xy * 0.006f + bendNoise * 0.008f) * refractionScale;

    float bgDepth = depthTex.SampleLevel(smp, screenUV, 0).r;
    float gelDepth = input.screenPos.z / input.screenPos.w;
    float depthDiff = LinearizeDepth(bgDepth) - LinearizeDepth(gelDepth);
    float depthFade = (bgDepth >= 0.999f) ? 1.0f : saturate(depthDiff / lerp(0.18f, 1.1f, softness));

    float3 sceneColor = grabTex.SampleLevel(smp, screenUV, 0).rgb;
    float3 refractedColor = grabTex.SampleLevel(smp, saturate(screenUV + distortion * depthFade), 0).rgb;

    float3 baseGel = float3(0.05f, 0.68f, 0.82f);
    float3 artistTint = lerp(baseGel, saturate(color.rgb), 0.34f);
    float3 gelHighlight = artistTint * (fresnel * 0.95f + bubbles * 0.45f + membranes * 0.16f);
    gelHighlight += float3(0.55f, 1.0f, 0.95f) * bubbles * 0.22f;
    gelHighlight *= intensity;

    float alpha = saturate(color.a * (0.10f + fresnel * 0.30f + membranes * 0.05f + bubbles * 0.09f));
    alpha *= lerp(0.56f, 0.88f, depthFade);

    float3 background = lerp(sceneColor, refractedColor, saturate(0.24f + fresnel * 0.18f));
    float3 finalColor = background + gelHighlight * (0.30f + alpha * 0.55f);

    return float4(finalColor, alpha);
}

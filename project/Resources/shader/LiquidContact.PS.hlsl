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
    for (int i = 0; i < 5; ++i)
    {
        value += Noise2D(p) * amp;
        p = p * 2.04f + float2(29.3f, 7.7f);
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
    float bandScale = max(effectScale, 0.05f);
    float softness = saturate(effectSoftness);
    float intensity = max(effectIntensity, 0.05f);
    bool magmaContact = effectType >= 0.5f;

    float2 planeUV = GetPlaneUV(input.localPos, input.normal);
    float contactDistance = abs(planeUV.x) / bandScale;
    float bandSoft = lerp(0.035f, 0.26f, softness);
    float band = 1.0f - smoothstep(0.0f, 1.0f + bandSoft, contactDistance);
    float edgeLine = 1.0f - smoothstep(0.0f, 0.12f + bandSoft, contactDistance);

    float2 flowUV = float2(planeUV.y * (2.0f + detail * 0.35f), contactDistance * (4.0f + detail * 0.35f));
    flowUV += float2(time * flowSpeedX * 0.35f, -time * speed * 0.55f + flowSpeedY * time * 0.22f);
    float broad = Fbm2(flowUV);
    float fine = Fbm2(flowUV * 2.7f + float2(-time * speed * 0.75f, time * 0.23f));
    float flecks = smoothstep(0.74f, 0.96f, fine + broad * 0.28f);
    float rollingFoam = smoothstep(0.38f, 0.78f, broad + sin(planeUV.y * (9.0f + detail) + time * speed * 2.2f) * 0.18f);

    float steamLift = saturate(1.0f - contactDistance * 0.72f);
    float steam = smoothstep(0.30f, 0.86f, Fbm2(float2(planeUV.y * 1.7f, input.worldPos.y * 1.3f - time * speed * 0.9f)));
    steam *= steamLift;

    float spark = smoothstep(0.88f, 0.985f, fine + Hash12(floor(flowUV * 5.0f)) * 0.18f) * edgeLine;
    float foam = saturate(band * (rollingFoam * 0.70f + flecks * 0.45f + edgeLine * 0.35f));
    float heat = saturate(band * (steam * 0.65f + flecks * 0.28f + spark * 0.90f));

    float bgDepth = depthTex.SampleLevel(smp, screenUV, 0).r;
    float liquidDepth = input.screenPos.z / input.screenPos.w;
    float depthDiff = LinearizeDepth(bgDepth) - LinearizeDepth(liquidDepth);
    float depthFade = (bgDepth >= 0.999f) ? 1.0f : saturate(depthDiff / 0.75f + 0.35f);

    float2 noisePush = float2(broad - 0.5f, fine - 0.5f);
    float2 tangentPush = normalize(float2(sign(planeUV.x) + 0.001f, 0.25f)) * (magmaContact ? heat : foam);
    float2 distortion = (noisePush * 0.012f + tangentPush * 0.010f) * (0.45f + softness) * intensity;

    float3 sceneColor = grabTex.SampleLevel(smp, screenUV, 0).rgb;
    float3 distortedScene = grabTex.SampleLevel(smp, saturate(screenUV + distortion), 0).rgb;

    float3 foamColor = lerp(float3(0.72f, 0.94f, 1.0f), saturate(color.rgb), 0.38f);
    foamColor += float3(0.55f, 0.95f, 1.0f) * flecks * 0.45f;

    float3 steamColor = lerp(float3(0.42f, 0.37f, 0.31f), float3(1.25f, 0.58f, 0.18f), heat);
    steamColor += float3(2.4f, 1.25f, 0.22f) * spark;
    steamColor = lerp(steamColor, saturate(color.rgb) * 1.4f, 0.22f);

    float contribution = magmaContact ? heat : foam;
    float alpha = saturate(contribution * color.a * depthFade * (magmaContact ? 0.78f : 0.86f));
    if (alpha < 0.01f)
    {
        discard;
    }

    float3 contactColor = magmaContact ? steamColor : foamColor;
    contactColor *= intensity;

    float3 finalColor = lerp(sceneColor, distortedScene, saturate(contribution * 0.65f));
    finalColor = lerp(finalColor, contactColor, saturate(alpha * 0.86f + edgeLine * 0.08f));
    finalColor += contactColor * (magmaContact ? spark * 0.18f : flecks * 0.06f);

    return float4(finalColor, alpha);
}

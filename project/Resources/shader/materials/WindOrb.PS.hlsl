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
    float2 cell = floor(p);
    float2 local = frac(p);
    float2 blend = local * local * (3.0f - 2.0f * local);
    float a = Hash12(cell);
    float b = Hash12(cell + float2(1.0f, 0.0f));
    float c = Hash12(cell + float2(0.0f, 1.0f));
    float d = Hash12(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);
}

float Fbm2(float2 p)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    [unroll]
    for (int octave = 0; octave < 4; ++octave)
    {
        value += Noise2D(p) * amplitude;
        p = p * 2.03f + float2(17.7f, 31.3f);
        amplitude *= 0.5f;
    }
    return value;
}

float RibbonMask(float coordinate, float curve, float width, float softness)
{
    float distanceToCurve = abs(coordinate - curve);
    return 1.0f - smoothstep(width, width + softness, distanceToCurve);
}

float4 main(VSOutput input) : SV_TARGET
{
    const float pi = 3.14159265f;
    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float intensity = max(effectIntensity, 0.05f);
    float softness = saturate(effectSoftness);
    float mode = clamp(effectType, 0.0f, 2.0f);
    float t = time * speed;

    float3 sphereNormal = normalize(input.localPos);
    float longitude = atan2(sphereNormal.z, sphereNormal.x);
    float latitude = asin(clamp(sphereNormal.y, -1.0f, 1.0f));

    float ribbonWidth = lerp(0.045f, 0.12f, softness) * lerp(1.0f, 0.78f, mode * 0.5f);
    float ribbonSoftness = lerp(0.022f, 0.085f, softness);
    float curveA = sin(longitude * 2.0f - t * 1.45f) * (0.22f + mode * 0.035f);
    float curveB = sin(longitude * 3.0f + t * 1.12f + 2.1f) * (0.19f + mode * 0.025f);
    float bandA = RibbonMask(latitude, curveA, ribbonWidth, ribbonSoftness);
    float bandB = RibbonMask(latitude, -curveB, ribbonWidth * 0.82f, ribbonSoftness);

    float3 tiltedAxis = normalize(float3(0.58f, 0.72f, -0.38f));
    float3 tiltedEast = normalize(cross(float3(0.0f, 1.0f, 0.0f), tiltedAxis));
    float3 tiltedForward = normalize(cross(tiltedAxis, tiltedEast));
    float tiltedLatitude = asin(clamp(dot(sphereNormal, tiltedAxis), -1.0f, 1.0f));
    float tiltedLongitude = atan2(dot(sphereNormal, tiltedForward), dot(sphereNormal, tiltedEast));
    float curveC = sin(tiltedLongitude * 2.0f - t * 1.28f + pi * 0.35f) * 0.15f;
    float bandC = RibbonMask(tiltedLatitude, curveC, ribbonWidth * 0.62f, ribbonSoftness * 0.82f);

    float flowA = 0.68f + 0.32f * smoothstep(-0.18f, 0.78f, sin(longitude * 9.0f - t * 6.8f + latitude * 4.0f));
    float flowB = 0.66f + 0.34f * smoothstep(-0.22f, 0.76f, sin(longitude * -11.0f + t * 7.4f - latitude * 3.0f));
    float flowC = 0.70f + 0.30f * smoothstep(-0.15f, 0.82f, sin(tiltedLongitude * 8.0f - t * 5.9f));
    float windBands = saturate(bandA * flowA + bandB * flowB * 0.82f + bandC * flowC * (0.56f + mode * 0.12f));

    float surfaceNoiseA = Fbm2(sphereNormal.xy * (2.8f + detail * 0.12f) + float2(t * 0.18f, -t * 0.13f));
    float surfaceNoiseB = Fbm2(sphereNormal.yz * (4.1f + detail * 0.08f) + float2(-t * 0.11f, t * 0.21f));
    float innerCurrent = smoothstep(0.48f, 0.82f, surfaceNoiseA * 0.62f + surfaceNoiseB * 0.48f);

    float3 worldNormal = normalize(input.normal);
    float3 viewDirection = normalize(cameraWorldPosition - input.worldPos);
    float fresnel = pow(1.0f - saturate(dot(worldNormal, viewDirection)), lerp(2.8f, 1.45f, softness));
    float3 lightDirection = normalize(-waterLightDirection);
    float diffuse = saturate(dot(worldNormal, lightDirection));
    float lightAmount = 0.58f + diffuse * min(waterLightIntensity, 2.5f) * 0.24f;

    float2 screenUV = input.screenPos.xy / input.screenPos.w * float2(0.5f, -0.5f) + 0.5f;
    screenUV = saturate(screenUV);
    float orbDepth = input.screenPos.z / input.screenPos.w;
    float sceneDepth = depthTex.SampleLevel(smp, screenUV, 0).r;
    float intersectionFade = sceneDepth >= 0.999f ? 1.0f : saturate((sceneDepth - orbDepth) * 180.0f + 0.22f);

    float2 flowDirection = float2(
        sin(longitude * 2.0f - t * 1.6f),
        cos(latitude * 3.0f + t * 1.3f));
    float refractionStrength = (0.0035f + windBands * 0.0075f + fresnel * 0.0025f) * max(effectScale, 0.05f);
    float2 distortion = (worldNormal.xy * 0.64f + flowDirection * 0.36f) * refractionStrength * intersectionFade;
    float3 sceneColor = grabTex.SampleLevel(smp, screenUV, 0).rgb;
    float3 refractedScene = grabTex.SampleLevel(smp, saturate(screenUV + distortion), 0).rgb;

    float3 artistTint = max(color.rgb, float3(0.03f, 0.03f, 0.03f));
    float3 coreTint = lerp(float3(0.12f, 0.72f, 0.62f), artistTint, 0.64f);
    float3 ribbonTint = lerp(float3(0.76f, 1.16f, 1.02f), artistTint * 1.18f, 0.24f);
    float3 transmission = lerp(sceneColor, refractedScene, 0.72f);
    float3 surfaceColor = coreTint * lightAmount * (0.70f + innerCurrent * 0.18f);
    surfaceColor += ribbonTint * windBands * intensity * 0.78f;
    surfaceColor += lerp(float3(0.45f, 1.0f, 0.88f), waterLightColor, 0.24f) * fresnel * intensity * 0.62f;
    float bodyWeight = saturate(0.62f + windBands * 0.25f + fresnel * 0.10f);
    float3 finalColor = lerp(transmission, surfaceColor, bodyWeight);

    float alpha = color.a * (0.58f + innerCurrent * 0.05f + windBands * 0.30f + fresnel * 0.16f);
    alpha = saturate(alpha * intersectionFade);
    if (alpha < 0.01f)
    {
        discard;
    }
    return float4(finalColor, alpha);
}

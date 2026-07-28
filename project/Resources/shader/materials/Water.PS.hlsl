#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
Texture2D<float4> bakedWaterFoamTex : register(t2);
Texture2D<float4> bakedWaterFlowTex : register(t3);
SamplerState smp : register(s0);

float2 Rotate2D(float2 p, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float LinearizeDepth(float z)
{
    const float nearClip = 0.1f;
    const float farClip = 1000.0f;
    return (nearClip * farClip) / max(farClip - z * (farClip - nearClip), 0.0001f);
}

float AntiAliasedThreshold(float value, float threshold)
{
    float width = max(fwidth(value) * 1.35f, 0.002f);
    return smoothstep(threshold - width, threshold + width, value);
}

float3 SafeNormalize(float3 value, float3 fallback)
{
    float lengthSq = dot(value, value);
    return lengthSq > 0.00001f ? value * rsqrt(lengthSq) : fallback;
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV = float2(screenUV.x * 0.5f + 0.5f, -screenUV.y * 0.5f + 0.5f);
    screenUV = saturate(screenUV);

    uint depthWidth = 1;
    uint depthHeight = 1;
    depthTex.GetDimensions(depthWidth, depthHeight);
    uint2 depthCoord = min(
        uint2(screenUV * float2(depthWidth, depthHeight)),
        uint2(depthWidth - 1, depthHeight - 1));

    float rawSceneDepth = depthTex.Load(int3(depthCoord, 0));
    float rawSurfaceDepth = saturate(input.screenPos.z / input.screenPos.w);

    // The depth buffer is sampled while no DSV is bound, so reject water hidden by opaque geometry explicitly.
    clip(rawSceneDepth - rawSurfaceDepth + 0.00001f);

    float sceneDepth = LinearizeDepth(rawSceneDepth);
    float surfaceDepth = LinearizeDepth(rawSurfaceDepth);
    float depthDifference = max(sceneDepth - surfaceDepth, 0.0f);
    float depthRange = max(effectScaleX, 1.0f);
    float depthFactor = saturate(depthDifference / depthRange);

    float2 worldXZ = input.worldPos.xz;
    float2 flowOffset = float2(uvOffsetX, uvOffsetY);
    float2 flowUVA = frac(worldXZ * 0.018f + flowOffset * 0.042f);
    float2 flowUVB = frac(Rotate2D(worldXZ, 0.83f) * 0.011f - flowOffset.yx * 0.031f + 0.37f);
    float4 flowA = bakedWaterFlowTex.Sample(smp, flowUVA);
    float4 flowB = bakedWaterFlowTex.Sample(smp, flowUVB);
    float2 flowWarp = ((flowA.rg + flowB.gr) - 1.0f) * 0.65f;

    float2 foamUV = frac(worldXZ * 0.013f + flowOffset * 0.026f + flowWarp * 0.08f);
    float4 foamTexture = bakedWaterFoamTex.Sample(smp, foamUV);
    float breakup = saturate(flowA.b * 0.44f + flowB.b * 0.28f + foamTexture.b * 0.28f);

    float detail = max(waveFrequency, 0.1f);
    float speed = max(waveSpeed, 0.02f);
    float majorPhase = dot(Rotate2D(worldXZ, 0.34f), float2(0.057f, 0.014f)) * detail;
    majorPhase += flowWarp.x * 1.45f + time * speed * 0.52f;
    float minorPhase = dot(Rotate2D(worldXZ, -0.71f), float2(0.036f, 0.010f)) * detail;
    minorPhase += flowWarp.y * 1.20f - time * speed * 0.31f;

    float majorWave = sin(majorPhase);
    float minorWave = sin(minorPhase);
    float majorBand = AntiAliasedThreshold(majorWave, 0.76f);
    float majorCore = AntiAliasedThreshold(majorWave, 0.94f);
    float minorBand = AntiAliasedThreshold(minorWave, 0.91f);
    float brokenMask = smoothstep(0.24f, 0.72f, breakup);

    float3 viewVector = cameraWorldPosition - input.worldPos;
    float cameraDistance = sqrt(max(dot(viewVector, viewVector), 0.0001f));
    float3 viewDirection = viewVector / cameraDistance;
    float patternDistanceFade = 1.0f - smoothstep(48.0f, 132.0f, cameraDistance);
    float patternStrength = max(billboardScale, 0.0f);
    float surfacePattern = majorBand * lerp(0.18f, 1.0f, brokenMask) + minorBand * 0.08f;
    surfacePattern *= patternStrength * patternDistanceFade * 0.34f;

    float normalStrength = saturate(waveHeight * 0.30f + 0.12f);
    float slopeX = cos(majorPhase) * 0.076f + cos(minorPhase) * 0.020f + flowWarp.x * 0.024f;
    float slopeZ = -sin(majorPhase) * 0.064f + cos(minorPhase) * 0.026f + flowWarp.y * 0.024f;
    float3 proceduralNormal = normalize(float3(-slopeX * normalStrength, 1.0f, -slopeZ * normalStrength));
    float3 baseNormal = SafeNormalize(input.normal, float3(0.0f, 1.0f, 0.0f));
    float3 waterNormal = normalize(lerp(baseNormal, proceduralNormal, 0.88f));

    float3 lightDirection = SafeNormalize(waterLightDirection, float3(-0.35f, -0.82f, 0.45f));
    float3 toLight = -lightDirection;
    float lightAmount = saturate(dot(waterNormal, toLight));
    float celLight = lerp(0.82f, 1.08f, smoothstep(0.28f, 0.52f, lightAmount));

    float tintMax = max(color.r, max(color.g, color.b));
    float tintMin = min(color.r, min(color.g, color.b));
    float tintLuminance = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
    float tintAmount = saturate((tintMax - tintMin) * 2.0f + (1.0f - tintLuminance) * 0.30f);
    float3 shallowColor = lerp(float3(0.40f, 0.84f, 0.98f), color.rgb * 1.05f, tintAmount * 0.48f);
    float3 middleColor = lerp(float3(0.09f, 0.49f, 0.79f), color.rgb * 0.78f, tintAmount * 0.40f);
    float3 deepColor = lerp(float3(0.025f, 0.18f, 0.45f), color.rgb * 0.50f, tintAmount * 0.34f);

    float shallowToMiddle = smoothstep(0.18f, 0.29f, depthFactor);
    float middleToDeep = smoothstep(0.62f, 0.73f, depthFactor);
    float3 waterTint = lerp(lerp(shallowColor, middleColor, shallowToMiddle), deepColor, middleToDeep);

    float refractionStrength = saturate(effectScale / 3.0f) * 0.034f;
    float2 refractedUV = saturate(screenUV + waterNormal.xz * refractionStrength * lerp(0.45f, 1.0f, depthFactor));
    float3 refractionColor = grabTex.Sample(smp, refractedUV).rgb;
    float3 bodyColor = lerp(refractionColor, waterTint, lerp(0.46f, 0.82f, depthFactor));
    bodyColor *= celLight;

    float fresnel = pow(1.0f - saturate(dot(viewDirection, waterNormal)), 3.0f);
    float3 reflectionDirection = reflect(-viewDirection, waterNormal);
    float skyBlend = smoothstep(-0.12f, 0.85f, reflectionDirection.y);
    float3 skyColor = lerp(float3(0.42f, 0.76f, 0.96f), float3(0.14f, 0.42f, 0.76f), skyBlend);
    bodyColor = lerp(bodyColor, skyColor, fresnel * 0.42f);

    float foamWidth = lerp(0.25f, 2.40f, saturate(effectSoftness));
    float validContact = smoothstep(0.035f, 0.13f, depthDifference);
    float outerContact = (1.0f - smoothstep(0.10f, foamWidth, depthDifference)) * validContact;
    float innerContact = (1.0f - smoothstep(0.055f, max(foamWidth * 0.32f, 0.12f), depthDifference)) * validContact;
    float foamBreakup = smoothstep(0.20f, 0.70f, foamTexture.r * 0.66f + flowA.b * 0.34f);
    float foamStrength = max(effectScaleZ, 0.0f);
    float outerFoam = outerContact * lerp(0.48f, 1.0f, foamBreakup) * foamStrength;
    float innerFoam = innerContact * smoothstep(0.38f, 0.76f, foamBreakup + majorCore * 0.22f) * foamStrength;

    float broadShade = sin(dot(worldXZ, float2(0.025f, 0.011f)) + time * speed * 0.24f);
    bodyColor *= 0.96f + broadShade * 0.055f;
    bodyColor += float3(0.28f, 0.68f, 0.96f) * surfacePattern * 0.10f;
    bodyColor += float3(0.52f, 0.82f, 1.02f) * majorCore * brokenMask * patternStrength * patternDistanceFade * 0.07f;
    bodyColor = lerp(bodyColor, float3(0.54f, 0.91f, 1.08f), saturate(outerFoam) * 0.68f);
    bodyColor = lerp(bodyColor, float3(1.12f, 1.18f, 1.20f), saturate(innerFoam) * 0.86f);

    float3 halfVector = SafeNormalize(toLight + viewDirection, toLight);
    float specularBase = saturate(dot(waterNormal, halfVector));
    float narrowSpecular = pow(specularBase, 96.0f);
    float specularBreakup = smoothstep(0.70f, 0.92f, breakup);
    float sparkleMask = majorCore * smoothstep(0.91f, 0.985f, breakup) * patternDistanceFade;
    float sparkleStrength = max(effectScaleY, 0.0f);
    float lightIntensity = max(waterLightIntensity, 0.15f);
    float3 lightColor = max(waterLightColor, float3(0.05f, 0.05f, 0.05f));
    float hdrHighlight = narrowSpecular * specularBreakup * 0.32f + sparkleMask * 0.48f;
    bodyColor += lightColor * hdrHighlight * sparkleStrength * lightIntensity;

    float brightness = clamp(0.78f + effectIntensity * 0.24f, 0.65f, 1.62f);
    bodyColor *= brightness;

    float artistAlpha = max(color.a, 0.35f);
    float alpha = (0.48f + depthFactor * 0.24f + fresnel * 0.12f + outerFoam * 0.08f + innerFoam * 0.10f) * artistAlpha;
    return float4(max(bodyColor, 0.0f), saturate(alpha));
}

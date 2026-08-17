#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);

float randomValue(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898f, 78.233f))) * 43758.5453123f);
}

float valueNoise(float2 st)
{
    float2 i = floor(st);
    float2 f = frac(st);

    float a = randomValue(i);
    float b = randomValue(i + float2(1.0f, 0.0f));
    float c = randomValue(i + float2(0.0f, 1.0f));
    float d = randomValue(i + float2(1.0f, 1.0f));

    float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0f - u.x) + (d - b) * u.x * u.y;
}

float2 GetFaceUV(VSOutput input)
{
    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 flatNormal = abs(normalize(cross(dx, dy)));

    if (flatNormal.y > 0.5f)
    {
        return input.worldPos.xz;
    }
    if (flatNormal.x > 0.5f)
    {
        return input.worldPos.zy * float2(1.0f, -1.0f);
    }
    return input.worldPos.xy * float2(1.0f, -1.0f);
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

    // The DSV is unbound while special materials sample scene depth, so reject
    // magma fragments hidden behind opaque geometry explicitly.
    clip(rawSceneDepth - rawSurfaceDepth + 0.00001f);

    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 flatNormal = abs(normalize(cross(dx, dy)));

    float speed = max(waveSpeed, 0.05f);
    float height = max(waveHeight, 0.0f);
    float detail = max(waveFrequency, 0.15f);
    float viscosity = max(effectScale, 0.1f);
    float crustWidth = saturate(effectSoftness);
    float heatIntensity = max(effectIntensity, 0.05f);

    float2 flowInput = float2(flowSpeedX, flowSpeedY);
    float timeScale = time * speed / (1.0f + viscosity * 0.7f);
    float2 faceUV = GetFaceUV(input);
    float uvScale = 0.34f * detail;
    float2 baseUV = faceUV * uvScale + float2(uvOffsetX, uvOffsetY);

    float sideGravity = 1.0f - saturate(flatNormal.y);
    baseUV += float2(flowInput.x, flowInput.y) * timeScale * 0.02f;
    baseUV.y -= sideGravity * timeScale * 0.025f;

    float2 warpSeed = baseUV + float2(timeScale * 0.015f, -timeScale * 0.011f);
    float2 warp = float2(
        valueNoise(warpSeed * 0.85f + float2(3.1f, timeScale * 0.01f)),
        valueNoise(warpSeed * 0.85f + float2(8.7f, -timeScale * 0.012f))
    ) - 0.5f;

    float2 magmaUV = baseUV + warp * (0.18f + viscosity * 0.045f);
    float n1 = valueNoise(magmaUV + float2(timeScale * 0.018f, timeScale * 0.010f));
    float n2 = valueNoise(magmaUV * 1.8f - float2(timeScale * 0.012f, timeScale * 0.023f));
    float n3 = valueNoise(magmaUV * 3.2f + float2(6.3f, -timeScale * 0.018f));

    float magmaNoise = saturate(n1 * 0.55f + n2 * 0.35f + n3 * 0.10f);
    magmaNoise = smoothstep(0.18f, 0.92f, magmaNoise);

    float crustMask = 1.0f - smoothstep(0.24f, lerp(0.48f, 0.62f, crustWidth), magmaNoise);
    float lavaMask = smoothstep(0.36f, 0.58f, magmaNoise);
    float hotMask = smoothstep(0.70f, 0.82f, magmaNoise);
    float coreMask = smoothstep(0.82f, 0.92f, magmaNoise + n3 * 0.04f);

    float3 materialTint = saturate(color.rgb);
    float tintPower = lerp(0.85f, 1.15f, materialTint.r);
    float3 darkCrust = float3(0.10f, 0.002f, 0.0f) * tintPower;
    float3 deepRed = float3(0.34f, 0.010f, 0.0f) * tintPower;
    float3 lavaRed = float3(1.0f, 0.06f, 0.01f) * tintPower;
    float3 orange = float3(1.0f, 0.25f, 0.015f) * heatIntensity;
    float3 yellow = float3(1.45f, 1.08f, 0.04f) * heatIntensity;

    float3 finalColor = lerp(darkCrust, deepRed, lavaMask);
    finalColor = lerp(finalColor, lavaRed, smoothstep(0.46f, 0.62f, magmaNoise));
    finalColor = lerp(finalColor, orange, smoothstep(0.58f, 0.74f, magmaNoise) * 0.85f);
    finalColor = lerp(finalColor, yellow, saturate(hotMask * 0.82f + coreMask * 0.42f));
    finalColor = lerp(finalColor, darkCrust, crustMask * 0.45f);

    float topLighting = lerp(0.58f, 1.0f, saturate(flatNormal.y + 0.15f));
    float sideGlow = lerp(0.88f, 1.08f, saturate(height * 0.12f));
    finalColor *= topLighting * sideGlow;

    return float4(saturate(finalColor), 1.0f);
}

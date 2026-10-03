#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);

// Integer hashing is stable across large world coordinates and triangle boundaries.
float MagmaHash(int2 cell)
{
    uint h = asuint(cell.x) * 1597334677u ^ asuint(cell.y) * 3812015801u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return float(h & 0x00ffffffu) / 16777215.0f;
}

float MagmaNoise(float2 p)
{
    int2 cell = int2(floor(p));
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    return lerp(lerp(MagmaHash(cell), MagmaHash(cell + int2(1, 0)), u.x),
        lerp(MagmaHash(cell + int2(0, 1)), MagmaHash(cell + int2(1, 1)), u.x), u.y);
}

float FilteredMagmaNoise(float2 p, float footprint)
{
    return lerp(0.5f, MagmaNoise(p), exp2(-3.0f * footprint * footprint));
}

float2 MagmaSlope(float2 p, float footprint)
{
    // Analytic derivatives keep the relief independent of mesh triangle boundaries.
    int2 cell = int2(floor(p));
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float2 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);
    float a = MagmaHash(cell);
    float b = MagmaHash(cell + int2(1, 0));
    float c = MagmaHash(cell + int2(0, 1));
    float d = MagmaHash(cell + int2(1, 1));
    return float2(b - a + (a - b - c + d) * u.y,
        c - a + (a - b - c + d) * u.x) * du
        * exp2(-3.0f * footprint * footprint);
}

float3 MagmaUnit(float3 v, float3 fallback)
{
    return dot(v, v) > 0.000001f ? normalize(v) : fallback;
}

float2 GetFaceUV(VSOutput input)
{
    // Authored normals keep projection stable on the coarse displaced sea mesh.
    float3 n = abs(normalize(input.normal));
    if (n.y >= max(n.x, n.z)) return input.worldPos.xz;
    if (n.x >= n.z) return input.worldPos.zy * float2(1.0f, -1.0f);
    return input.worldPos.xy * float2(1.0f, -1.0f);
}

float3 MagmaLinearColor(float3 displayColor)
{
    // Palette constants are authored in display space; the render target is linear HDR.
    return pow(displayColor, 2.2f);
}

float4 main(VSOutput input) : SV_TARGET
{
    uint depthWidth, depthHeight;
    depthTex.GetDimensions(depthWidth, depthHeight);
    uint2 depthCoord = min(uint2(input.pos.xy), uint2(depthWidth - 1, depthHeight - 1));
    float rawSceneDepth = depthTex.Load(int3(depthCoord, 0));
    // SV_POSITION provides rasterized coordinates and depth without an extra divide.
    clip(rawSceneDepth - input.pos.z + 0.00001f);

    // Living magma slime: the body uses local height, the geometric face uses a UV tag.
    // Keep the submerged portion hidden in both anticipation and airborne states.
    if (effectType > 4.5f && effectType < 5.5f)
    {
        clip(input.worldPos.y - effectScaleY);
        float3 n = MagmaUnit(input.normal, float3(0, 1, 0));
        float3 v = MagmaUnit(cameraWorldPosition - input.worldPos, n);
        float3 l = MagmaUnit(float3(-0.4f, 0.8f, -0.3f), float3(0, 1, 0));
        float3 h = MagmaUnit(l + v, n);
        float shine = pow(saturate(dot(n, h)), 48.0f);
        if (input.uv.x > 1.5f)
        {
            float3 face = MagmaLinearColor(float3(0.16f, 0.055f, 0.035f));
            return float4(face + shine * float3(0.12f, 0.075f, 0.04f), 1.0f);
        }
        float molten = MagmaNoise(input.localPos.xz * 3.5f + time * float2(0.09f, -0.07f));
        float warmth = 1.0f - smoothstep(-0.60f, 0.45f, input.localPos.y + (molten - 0.5f) * 0.16f);
        float3 red = MagmaLinearColor(float3(0.98f, 0.23f, 0.045f));
        float3 gold = MagmaLinearColor(float3(1.0f, 0.76f, 0.12f));
        float3 skin = lerp(red, gold, warmth * 0.90f);
        float light = 0.72f + saturate(dot(n, l)) * 0.28f;
        float rim = pow(1.0f - saturate(dot(n, v)), 3.0f);
        skin = skin * light + float3(1.0f, 0.82f, 0.58f) * shine * 0.28f;
        skin += float3(0.09f, 0.026f, 0.002f) * rim;
        return float4(skin * max(effectIntensity, 0.0f), 1.0f);
    }

    // Bubble caps share the magma pass but have a smooth, readable liquid skin.
    // effectScaleY is the liquid surface; softness opens the cap into a spreading rim.
    if (effectType > 3.5f && effectType < 4.5f)
    {
        clip(input.worldPos.y - effectScaleY);
        clip(length(input.localPos.xz) - saturate(effectSoftness) * 0.88f);
        float3 n = MagmaUnit(input.normal, float3(0, 1, 0));
        float3 v = MagmaUnit(cameraWorldPosition - input.worldPos, n);
        float3 l = MagmaUnit(-waterLightDirection, float3(0, 1, 0));
        float3 h = MagmaUnit(l + v, n);
        float warmth = saturate(n.y * 0.65f + MagmaNoise(input.worldPos.xz * 0.4f - time * 0.12f) * 0.35f);
        float3 skin = lerp(MagmaLinearColor(float3(0.76f, 0.27f, 0.06f)),
            MagmaLinearColor(float3(1.0f, 0.55f, 0.15f)), warmth);
        float glint = pow(saturate(dot(n, h)), 28.0f) * 0.14f;
        skin += float3(1.0f, 0.66f, 0.25f) * glint;
        return float4(skin * max(effectIntensity, 0.0f), 1.0f);
    }

    float detail = max(waveFrequency, 0.15f);
    float viscosity = max(effectScale, 0.1f);
    float t = time * max(waveSpeed, 0.0f) / (1.0f + viscosity * 0.7f);
    float2 flow = float2(flowSpeedX, flowSpeedY);
    float2 p = GetFaceUV(input) * (0.022f * detail)
        + float2(uvOffsetX, uvOffsetY) - flow * t * 0.55f;
    float footprint = max(length(ddx(p)), length(ddy(p)));

    // Two evolving scales break up patches without repeated sine bands.
    float2 warp = float2(
        FilteredMagmaNoise(p * 0.42f + float2(3.7f, t * 0.018f), footprint * 0.42f),
        FilteredMagmaNoise(p * 0.42f + float2(11.1f, -t * 0.014f), footprint * 0.42f)) - 0.5f;
    float2 q = p + warp * (0.9f + min(viscosity, 4.0f) * 0.12f);
    // Conservative unwarped footprints avoid filtering jumps with patch slopes.
    float broad = FilteredMagmaNoise(q, footprint * 1.6f);
    float medium = FilteredMagmaNoise(q * 2.37f + float2(7.3f, t * 0.012f), footprint * 3.8f);
    float fine = FilteredMagmaNoise(q * 5.13f + float2(-t * 0.022f, 13.6f), footprint * 8.2f);
    float2 grainUV = float2(q.x * 0.8f - q.y * 0.6f, q.x * 0.6f + q.y * 0.8f);
    float grain = FilteredMagmaNoise(grainUV * 14.7f + float2(31.4f, -t * 0.035f), footprint * 23.5f);
    float crustField = broad * 0.25f + medium * 0.43f + fine * 0.24f + grain * 0.08f;
    // Most of the surface stays molten. Softness controls cooled island coverage.
    float crustThreshold = lerp(0.65f, 0.41f, saturate(effectSoftness));
    float crustMask = smoothstep(crustThreshold, crustThreshold + 0.09f, crustField);
    // Small molten breaks split the cooled patches instead of leaving solid stains.
    crustMask *= 1.0f - smoothstep(0.68f, 0.84f, grain * 0.72f + fine * 0.28f) * 0.22f;

    float heat = FilteredMagmaNoise(q * 0.71f + float2(17.2f, -t * 0.027f), footprint * 1.14f);
    float hotMask = smoothstep(0.70f, 0.89f, heat * 0.58f + medium * 0.27f + fine * 0.15f);
    float moltenVariation = smoothstep(0.24f, 0.76f, heat * 0.40f + medium * 0.38f + fine * 0.22f);
    // Leave green-channel headroom for the scene's contrast and saturation grading.
    float3 molten = lerp(MagmaLinearColor(float3(0.82f, 0.31f, 0.10f)),
        MagmaLinearColor(float3(0.96f, 0.44f, 0.15f)), moltenVariation);
    molten = lerp(molten, MagmaLinearColor(float3(1.0f, 0.76f, 0.22f)), hotMask * 0.85f);
    molten *= 0.78f + fine * 0.28f + grain * 0.16f;
    float3 crust = lerp(MagmaLinearColor(float3(0.29f, 0.22f, 0.18f)),
        MagmaLinearColor(float3(0.46f, 0.32f, 0.23f)), fine * 0.65f + grain * 0.35f);
    float3 finalColor = lerp(molten, crust, crustMask * 0.98f);
    float tintPower = lerp(0.85f, 1.15f, saturate(color.r));
    finalColor *= max(effectIntensity, 0.0f) * tintPower;

    // Shade small surface relief, while molten emission remains visible in shadow.
    float3 baseNormal = MagmaUnit(input.normal, float3(0, 1, 0));
    float3 axis = abs(baseNormal);
    float3 tangent, bitangent;
    if (axis.y >= max(axis.x, axis.z)) {
        tangent = float3(1, 0, 0); bitangent = float3(0, 0, 1);
    } else if (axis.x >= axis.z) {
        tangent = float3(0, 0, 1); bitangent = float3(0, -1, 0);
    } else {
        tangent = float3(1, 0, 0); bitangent = float3(0, -1, 0);
    }
    float2 slope = MagmaSlope(p * 3.7f + float2(t * 0.021f, 4.8f), footprint * 3.7f) * 0.32f
        + MagmaSlope(p * 8.6f + float2(12.1f, -t * 0.029f), footprint * 8.6f) * 0.17f;
    slope *= clamp(0.70f + max(waveHeight, 0.0f) * 4.0f, 0.70f, 1.5f);
    float3 n = normalize(baseNormal - tangent * slope.x - bitangent * slope.y);
    float3 l = MagmaUnit(-waterLightDirection, float3(0, 1, 0));
    float3 v = MagmaUnit(cameraWorldPosition - input.worldPos, baseNormal);
    float3 h = MagmaUnit(l + v, baseNormal);
    float nDotL = saturate(dot(n, l));
    finalColor *= lerp(0.91f + nDotL * 0.09f, 0.52f + nDotL * 0.48f, crustMask);
    float roughHighlight = pow(saturate(dot(n, h)), lerp(42.0f, 16.0f, crustMask));
    float highlightStrength = roughHighlight * lerp(0.065f, 0.095f, crustMask) * nDotL;
    finalColor += max(waterLightColor, 0.0f) * clamp(waterLightIntensity, 0.0f, 3.0f) * highlightStrength;

    // Preserve the upward-travelling cue on dedicated geyser geometry.
    if (effectType > 2.5f && effectType < 3.5f)
    {
        float streak = 0.5f + 0.5f * sin(input.worldPos.y * 2.2f - time * 15.0f
            + MagmaNoise(input.worldPos.xz * 0.8f) * 4.0f);
        finalColor = lerp(float3(0.80f, 0.09f, 0.008f), float3(1.0f, 0.64f, 0.08f),
            saturate(streak * 0.65f + crustField * 0.35f));
    }
    return float4(finalColor, 1.0f);
}

#include "../common/Water.hlsli"

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

    float a = Hash12(i + float2(0.0f, 0.0f));
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
        p = p * 2.03f + float2(17.1f, 31.7f);
        amp *= 0.5f;
    }

    return value;
}

VSOutput main(VSInput input)
{
    VSOutput output;

    float3 centerWorld = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), world).xyz;

    // Type 4 keeps the crossed fire cards in object space so the flame has real depth.
    if (effectType >= 3.5f && effectType < 4.5f)
    {
        float cardIndex = floor(max(input.uv.x, 0.0f) * 0.5f + 0.001f);
        float2 cardUV = float2(input.uv.x - cardIndex * 2.0f, input.uv.y);
        float height01 = saturate(1.0f - cardUV.y);
        float topWeight = height01 * height01;
        float baseScale = max(billboardScale, 0.05f) * max(effectScale, 0.05f);
        float motion = saturate(abs(flowSpeedY));
        float wind = clamp(flowSpeedX, -1.0f, 1.0f);
        float phase = cardIndex * 2.37f + centerWorld.x * 0.11f + centerWorld.z * 0.17f;
        float curl = Fbm2(float2(height01 * 2.1f + phase, time * 0.72f + phase * 0.31f));
        float gust = sin(time * (3.6f + motion * 1.8f) + phase) * 0.5f + 0.5f;

        float3 localPosition = input.pos.xyz;
        localPosition.x *= baseScale * max(effectScaleX, 0.12f);
        localPosition.y *= baseScale * max(effectScaleY, 0.12f);
        localPosition.z *= baseScale * max(effectScaleZ, 0.12f);

        float lateralSway = wind * (0.055f + motion * 0.10f);
        lateralSway += (curl - 0.5f) * (0.075f + motion * 0.055f);
        lateralSway += (gust - 0.5f) * 0.035f;
        localPosition.x += lateralSway * baseScale * topWeight;
        localPosition.z += sin(time * 2.75f + phase * 1.43f) * 0.038f * baseScale * topWeight;

        float breathing = 1.0f + (curl - 0.5f) * (0.075f + motion * 0.035f) * topWeight;
        localPosition.x *= breathing;
        localPosition.z *= breathing;

        float4 localPosition4 = float4(localPosition, 1.0f);
        output.pos = mul(localPosition4, WVP);
        output.worldPos = mul(localPosition4, world).xyz;
        output.screenPos = output.pos;
        output.localPos = float3(cardUV.x * 2.0f - 1.0f, height01, cardIndex);
        output.normal = normalize(mul(input.normal, (float3x3)WorldInverseTranspose));
        output.uv = cardUV;
        return output;
    }

    float3 cameraToCenter = centerWorld - cameraWorldPosition;
    float cameraDistance = max(length(cameraToCenter), 0.0001f);
    float3 viewForward = cameraToCenter / cameraDistance;
    float3 upSeed = (abs(viewForward.y) > 0.96f) ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    float3 viewRight = normalize(cross(upSeed, viewForward));
    float3 viewUp = normalize(cross(viewForward, viewRight));

    float3 axisX = float3(world._11, world._12, world._13);
    float3 axisY = float3(world._21, world._22, world._23);
    float3 axisZ = float3(world._31, world._32, world._33);
    float axisXLength = max(length(axisX), 0.0001f);
    float axisYLength = max(length(axisY), 0.0001f);
    float axisZLength = max(length(axisZ), 0.0001f);

    float proxyRadius = max(max(axisXLength, axisYLength), axisZLength);
    proxyRadius *= max(billboardScale, 0.05f) * max(effectScale, 0.05f);
    proxyRadius = max(proxyRadius, 0.001f);

    float2 quad = input.pos.xy;
    float2 phaseSeed = float2(uvOffsetX, uvOffsetY) * float2(0.37f, 0.23f);
    float2 noiseSeed = quad * 1.4f + input.uv * 0.8f + phaseSeed;
    float slowCurl = Fbm2(noiseSeed + float2(time * 0.22f, -time * 0.37f));
    float fastCurl = Fbm2(noiseSeed * 1.8f + float2(-time * 0.76f, time * 0.54f));
    float motion = saturate(abs(flowSpeedY));
    float wind = clamp(flowSpeedX, -1.0f, 1.0f);
    float height01 = saturate(quad.y * 0.5f + 0.5f);
    float topWeight = height01 * height01;
    float flameMode = (effectType < 0.5f || (effectType >= 1.5f && effectType < 2.5f)) ? 1.0f : 0.0f;
    float gust = sin(time * (4.6f + motion * 2.2f) + phaseSeed.x * 3.1f) * 0.025f;
    quad.x += (wind * (0.08f + motion * 0.18f) + gust) * topWeight * flameMode;
    float2 flutter = float2(slowCurl - 0.5f, fastCurl - 0.5f) * (0.028f + motion * 0.026f);

    float3 worldOffset = viewRight * (quad.x + flutter.x) * proxyRadius;
    worldOffset += viewUp * (quad.y + flutter.y) * proxyRadius;
    float3 billboardWorldPos = centerWorld + worldOffset;

    float3 axisXNormal = axisX / axisXLength;
    float3 axisYNormal = axisY / axisYLength;
    float3 axisZNormal = axisZ / axisZLength;
    float3 billboardLocalPos = float3(
        dot(worldOffset, axisXNormal) / axisXLength,
        dot(worldOffset, axisYNormal) / axisYLength,
        dot(worldOffset, axisZNormal) / axisZLength
    );

    output.pos = mul(float4(billboardLocalPos, 1.0f), WVP);
    output.worldPos = billboardWorldPos;
    output.screenPos = output.pos;
    output.localPos = float3(quad, 0.0f);
    output.normal = -viewForward;
    output.uv = input.uv;

    return output;
}

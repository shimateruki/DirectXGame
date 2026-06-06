#include "Water.hlsli"

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
    float2 noiseSeed = quad * 1.4f + input.uv * 0.8f;
    float slowCurl = Fbm2(noiseSeed + float2(time * 0.22f, -time * 0.37f));
    float fastCurl = Fbm2(noiseSeed * 1.8f + float2(-time * 0.76f, time * 0.54f));
    float2 flutter = float2(slowCurl - 0.5f, fastCurl - 0.5f) * 0.035f;

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

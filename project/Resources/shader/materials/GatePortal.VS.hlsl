#include "../common/Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;

    float3 axisX = float3(world._11, world._12, world._13);
    float3 axisY = float3(world._21, world._22, world._23);
    float3 axisZ = float3(world._31, world._32, world._33);
    float axisXLength = max(length(axisX), 0.0001f);
    float axisYLength = max(length(axisY), 0.0001f);
    float axisZLength = max(length(axisZ), 0.0001f);
    float proxyRadius = max(max(axisXLength, axisYLength), axisZLength);
    proxyRadius *= max(billboardScale, 0.05f);

    float2 quad = input.pos.xy;
    float layerDepth = input.pos.z;
    float2 portalScale = float2(max(effectScaleX, 0.05f), max(effectScaleY, 0.05f));
    float portalDepthScale = max(effectScaleZ, 0.0f);
    float3 axisZNormal = axisZ / axisZLength;
    float3 portalLocalPos = float3(0.0f, 0.53f, 0.0f) + float3(
        quad.x * proxyRadius * portalScale.x / axisXLength,
        quad.y * proxyRadius * portalScale.y / axisYLength,
        layerDepth * proxyRadius * portalDepthScale * 0.18f / axisZLength
    );
    float3 portalWorldPos = mul(float4(portalLocalPos, 1.0f), world).xyz;

    output.pos = mul(float4(portalLocalPos, 1.0f), WVP);
    output.worldPos = portalWorldPos;
    output.screenPos = output.pos;
    output.localPos = float3(quad, layerDepth);
    output.normal = axisZNormal;
    output.uv = input.uv;

    return output;
}

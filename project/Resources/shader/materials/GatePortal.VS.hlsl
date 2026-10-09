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
    float3 portalLocalPos;
    if (effectType > 3.5f)
    {
        // The arena barrier uses a continuous cylindrical proxy and unwrapped UVs.
        portalLocalPos = float3(input.pos.x * portalScale.x,
            input.pos.y * portalScale.y, input.pos.z * max(effectScaleZ, 0.05f)) *
            max(billboardScale, 0.05f);
    }
    else if (effectType > 2.5f)
    {
        // Arena barriers must preserve the authored rectangular wall dimensions.
        // The circular gate path intentionally uses the largest axis instead.
        portalLocalPos = float3(
            quad.x * portalScale.x * max(billboardScale, 0.05f),
            quad.y * portalScale.y * max(billboardScale, 0.05f),
            layerDepth * portalDepthScale * 0.18f
        );
    }
    else
    {
        portalLocalPos = float3(0.0f, 0.53f, 0.0f) + float3(
            quad.x * proxyRadius * portalScale.x / axisXLength,
            quad.y * proxyRadius * portalScale.y / axisYLength,
            layerDepth * proxyRadius * portalDepthScale * 0.18f / axisZLength
        );
    }
    float3 portalWorldPos = mul(float4(portalLocalPos, 1.0f), world).xyz;

    output.pos = mul(float4(portalLocalPos, 1.0f), WVP);
    output.worldPos = portalWorldPos;
    output.screenPos = output.pos;
    output.localPos = effectType > 3.5f
        ? float3(input.uv.x * 2.0f - 1.0f, input.uv.y * 2.0f - 1.0f, layerDepth)
        : float3(quad, layerDepth);
    output.normal = effectType > 3.5f
        ? normalize(mul(float4(input.normal, 0.0f), WorldInverseTranspose).xyz)
        : axisZNormal;
    output.uv = input.uv;

    return output;
}

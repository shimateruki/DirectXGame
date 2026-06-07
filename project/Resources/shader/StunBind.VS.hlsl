#include "Water.hlsli"

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

    float2 quad = input.pos.xy;
    float jitter = sin(time * max(waveSpeed, 0.05f) * 18.0f + dot(quad, float2(9.0f, 13.0f))) * 0.012f;
    float3 worldOffset = viewRight * (quad.x + jitter) * proxyRadius + viewUp * (quad.y - jitter) * proxyRadius;
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

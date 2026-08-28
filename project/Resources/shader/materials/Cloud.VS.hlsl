#include "../common/Water.hlsli"

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
    float speed = max(waveSpeed, 0.12f);
    float objectPhase = dot(centerWorld.xz, float2(0.071f, 0.113f));
    float motionPhase = time * (0.32f + speed * 0.70f) + objectPhase;

    // Move the whole cloud mass slowly while keeping a smaller independent edge wobble.
    float horizontalDrift = sin(motionPhase) * proxyRadius * 0.055f;
    float verticalBob = sin(motionPhase * 0.73f + objectPhase * 0.61f) * proxyRadius * 0.018f;
    float edgeWobble = sin(motionPhase * 1.35f + quad.x * 1.7f + quad.y * 2.1f) * 0.032f;
    float3 worldOffset = viewRight * (quad.x + edgeWobble) * proxyRadius + viewUp * quad.y * proxyRadius;
    worldOffset += viewRight * horizontalDrift + viewUp * verticalBob;
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

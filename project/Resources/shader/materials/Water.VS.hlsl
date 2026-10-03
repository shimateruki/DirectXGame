#include "../common/Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 flatLocalPos = float4(input.pos.x, 0.0f, input.pos.z, 1.0f);
    float2 worldXZ = mul(flatLocalPos, world).xz;

    float worldDisplacement = WaterSwell(worldXZ).x;
    // The calm variant keeps the surface flat; only the shading flows.
    if (effectType >= 1.5f) worldDisplacement = 0.0f;
    float worldScaleY = max(length(mul(float3(0.0f, 1.0f, 0.0f), (float3x3)world)), 0.001f);
    float localDisplacement = worldDisplacement / worldScaleY;

    float4 localPos = float4(input.pos.x, localDisplacement, input.pos.z, 1.0f);
    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    output.localPos = float3(input.pos.xz, 1.0f);
    output.normal = normalize(mul(float3(0.0f, 1.0f, 0.0f), (float3x3)world));
    output.uv = input.uv;

    return output;
}

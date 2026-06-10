#include "Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;

    float scale = max(billboardScale, 0.05f);
    float4 localPos = float4(input.pos.xy * scale, 0.0f, 1.0f);
    float4 worldPos = mul(localPos, world);

    output.pos = mul(localPos, WVP);
    output.worldPos = worldPos.xyz;
    output.screenPos = output.pos;
    output.localPos = float3(input.pos.xy, 0.0f);
    output.normal = normalize(mul(float4(0.0f, 0.0f, -1.0f, 0.0f), WorldInverseTranspose).xyz);
    output.uv = input.uv;

    return output;
}

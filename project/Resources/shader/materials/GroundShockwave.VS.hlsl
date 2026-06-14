#include "../common/Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 localPos = input.pos;
    float3 worldNormal = normalize(mul(input.normal, (float3x3)WorldInverseTranspose));

    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    output.localPos = localPos.xyz;
    output.normal = worldNormal;
    output.uv = input.uv;

    return output;
}

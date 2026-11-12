#include "../common/Water.hlsli"
VSOutput main(VSInput input)
{
    VSOutput output;
    output.pos = mul(input.pos, WVP);
    output.screenPos = output.pos;
    output.worldPos = mul(input.pos, world).xyz;
    output.normal = normalize(mul(input.normal, (float3x3)WorldInverseTranspose));
    output.uv = input.uv;
    output.localPos = input.pos.xyz;
    return output;
}

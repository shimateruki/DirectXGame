#include "Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;
    
    // 氷は波打たないので、サイン波などの計算は一切せず、そのまま座標変換するだけ！
    output.pos = mul(input.pos, WVP);
    output.worldPos = mul(input.pos, world).xyz;
    output.screenPos = output.pos;
    
    output.normal = normalize(mul(input.normal, (float3x3) world));
    output.uv = input.uv;
    
    return output;
}
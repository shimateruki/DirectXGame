#include "Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;
    
    float4 localPos = input.pos;

    // 炎の「ゆらぎ」を頂点でも表現する
    // Y座標が高いほど（先端ほど）大きく揺れるようにする
    float swayMask = saturate(localPos.y * 0.8f); 
    
    // 複数のサイン波を組み合わせて不規則な揺れを作る
    float swayX = sin(time * 6.0f + localPos.y * 1.5f) * 0.15f * swayMask;
    float swayZ = cos(time * 5.2f + localPos.y * 1.2f) * 0.15f * swayMask;
    
    // 上に行くほど少しだけ内側に絞る（炎の形）
    float taper = 1.0f - (saturate(localPos.y * 0.2f) * 0.3f);
    localPos.xz *= taper;
    
    localPos.x += swayX;
    localPos.z += swayZ;

    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    output.normal = normalize(mul(input.normal, (float3x3)world));
    output.uv = input.uv;
    
    return output;
}

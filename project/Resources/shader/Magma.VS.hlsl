#include "Water.hlsli"

float random(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}
float noise(float2 st)
{
    float2 i = floor(st);
    float2 f = frac(st);
    float a = random(i);
    float b = random(i + float2(1.0, 0.0));
    float c = random(i + float2(0.0, 1.0));
    float d = random(i + float2(1.0, 1.0));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 localPos = input.pos;
    float topMask = smoothstep(0.0f, 0.1f, input.pos.y);

    // サイン波を削除！超低周波のノイズだけを使い、重くゆっくりとした隆起を作る
    float2 worldXZ = mul(localPos, world).xz;
    float magmaHeave = noise(worldXZ * 0.05f + time * 0.05f);
    
    localPos.y += magmaHeave * waveHeight * topMask;

    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    output.localPos = localPos.xyz;
    
    output.normal = normalize(mul(input.normal, (float3x3) world));
    output.uv = input.uv;
    
    return output;
}

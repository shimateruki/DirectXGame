#include "Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;
    
    float4 localPos = input.pos;
    
    float phaseX = localPos.x * waveFrequency + time * waveSpeed;
    float phaseZ = localPos.z * waveFrequency + time * waveSpeed * 0.8f;
    float waveX = sin(phaseX);
    float waveZ = cos(phaseZ);

    float topMask = step(0.9f, input.normal.y);

    localPos.y += (waveX + waveZ) * waveHeight * topMask;

    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    
    // 法線の計算
    float dy_dx = waveHeight * waveFrequency * cos(phaseX) * topMask;
    float dy_dz = waveHeight * waveFrequency * -sin(phaseZ) * 0.8f * topMask;
    float3 waveNormal = normalize(float3(-dy_dx, 1.0f, -dy_dz));
    
    float3 finalNormal = lerp(input.normal, waveNormal, topMask);

    output.normal = normalize(mul(finalNormal, (float3x3) world));
    output.uv = input.uv;
    
    return output;
}

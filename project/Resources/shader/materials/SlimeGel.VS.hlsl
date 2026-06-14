#include "../common/Water.hlsli"

float Hash31(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

VSOutput main(VSInput input)
{
    VSOutput output;

    float3 localPos = input.pos.xyz;
    float3 localNormal = normalize(input.normal);
    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float wobblePower = waveHeight * 0.035f;

    float phaseA = dot(localPos, float3(1.7f, 0.8f, 1.2f)) * detail + time * speed * 2.1f;
    float phaseB = dot(localPos, float3(-0.6f, 1.9f, 1.4f)) * detail - time * speed * 1.55f;
    float surfaceRipple = sin(phaseA) * 0.55f + cos(phaseB) * 0.45f;
    float slowPulse = sin(time * speed * 1.15f + Hash31(abs(localPos) + 0.17f) * 6.28318f);

    localPos += localNormal * (surfaceRipple * wobblePower + slowPulse * wobblePower * 0.35f);

    float3 worldPos = mul(float4(localPos, 1.0f), world).xyz;
    float3 worldNormal = normalize(mul(localNormal, (float3x3)WorldInverseTranspose));

    output.pos = mul(float4(localPos, 1.0f), WVP);
    output.worldPos = worldPos;
    output.screenPos = output.pos;
    output.localPos = localPos;
    output.normal = worldNormal;
    output.uv = input.uv;

    return output;
}

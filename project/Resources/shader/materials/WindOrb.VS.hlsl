#include "../common/Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;

    float3 localNormal = normalize(input.normal);
    float3 localPos = input.pos.xyz;
    float speed = max(waveSpeed, 0.05f);
    float detail = max(waveFrequency, 0.1f);
    float longitude = atan2(localPos.z, localPos.x);
    float rippleA = sin(longitude * 3.0f - time * speed * 3.8f + localPos.y * detail * 0.42f);
    float rippleB = sin(longitude * -5.0f + time * speed * 2.6f - localPos.y * detail * 0.27f);
    float deformation = (rippleA * 0.65f + rippleB * 0.35f) * max(waveHeight, 0.0f) * 0.012f;
    localPos += localNormal * deformation;

    float3 worldPosition = mul(float4(localPos, 1.0f), world).xyz;
    output.pos = mul(float4(localPos, 1.0f), WVP);
    output.worldPos = worldPosition;
    output.normal = normalize(mul(localNormal, (float3x3)WorldInverseTranspose));
    output.uv = input.uv;
    output.localPos = localPos;
    output.screenPos = output.pos;
    return output;
}

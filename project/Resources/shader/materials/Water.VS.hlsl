#include "../common/Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;

    float detail = max(waveFrequency, 0.1f);
    float speed = max(waveSpeed, 0.02f);
    float height = max(waveHeight, 0.0f);
    float2 localXZ = input.pos.xz;

    float broadPhaseA = dot(localXZ, float2(1.15f, 0.42f)) * detail * 0.72f + time * speed * 0.52f;
    float broadPhaseB = dot(localXZ, float2(-0.48f, 1.08f)) * detail * 0.58f - time * speed * 0.37f;
    float detailPhase = dot(localXZ, float2(1.72f, -1.34f)) * detail * 1.12f + time * speed * 0.81f;

    float broadSwell = sin(broadPhaseA) * 0.58f + sin(broadPhaseB) * 0.34f;
    float detailSwell = sin(detailPhase) * 0.08f;
    float displacement = (broadSwell + detailSwell) * height * 0.12f;

    float4 localPos = float4(input.pos.x, displacement, input.pos.z, 1.0f);
    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    output.localPos = float3(localXZ, 1.0f);
    output.normal = normalize(mul(float3(0.0f, 1.0f, 0.0f), (float3x3)world));
    output.uv = input.uv;

    return output;
}

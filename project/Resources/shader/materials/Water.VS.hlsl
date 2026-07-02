#include "../common/Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;

    float detail = max(waveFrequency, 0.1f);
    float speed = max(waveSpeed, 0.02f);
    float height = max(abs(waveHeight), 0.08f);
    float oceanScale = max(effectScale, 0.25f);
    float2 flow = float2(flowSpeedX, flowSpeedY);
    float2 broadCoord = input.pos.xz * detail * 0.035f + flow * time * 0.14f;
    float2 midCoord = input.pos.xz * detail * 0.075f - flow.yx * time * 0.20f;

    float broadSwell =
        sin(dot(broadCoord, float2(1.14f, 0.38f)) + time * speed * 0.46f) * 0.46f +
        sin(dot(broadCoord, float2(-0.52f, 1.06f)) - time * speed * 0.36f) * 0.34f +
        sin(dot(broadCoord, float2(0.28f, 0.74f)) + time * speed * 0.22f) * 0.20f;
    float crossRipple =
        sin(dot(midCoord, float2(1.90f, -0.45f)) + time * speed * 0.86f) * 0.42f +
        sin(dot(midCoord, float2(-1.12f, 1.55f)) - time * speed * 0.72f) * 0.34f;

    // Rebuild the source mesh as one continuous water sheet, then add layered ocean swell.
    float displacement = (broadSwell * 0.18f + crossRipple * 0.038f) * height * oceanScale;
    float4 localPos = float4(input.pos.x, displacement, input.pos.z, 1.0f);

    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    output.localPos = float3(input.pos.xz, 1.0f);

    // Keep the base normal stable; fine waves are rebuilt per pixel.
    output.normal = normalize(mul(float3(0.0f, 1.0f, 0.0f), (float3x3) world));
    output.uv = input.uv;

    return output;
}

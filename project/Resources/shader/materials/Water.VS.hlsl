#include "../common/Water.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;

    float detail = max(waveFrequency, 0.1f);
    float speed = max(waveSpeed, 0.02f);
    float height = max(abs(waveHeight), 0.08f);
    float2 waveCoord = input.pos.xz * detail * 0.055f + float2(flowSpeedX, flowSpeedY) * time * 0.22f;
    float slowSwell =
        sin(waveCoord.x * 1.25f + waveCoord.y * 0.55f + time * speed * 0.65f) * 0.55f +
        sin(waveCoord.x * -0.42f + waveCoord.y * 1.10f - time * speed * 0.48f) * 0.45f;

    // Rebuild the source mesh as one continuous water sheet, then add a subtle analytic swell.
    float4 localPos = float4(input.pos.x, slowSwell * height * 0.055f, input.pos.z, 1.0f);

    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    output.localPos = float3(input.pos.xz, 1.0f);

    // Keep the base normal stable; fine waves are rebuilt per pixel.
    output.normal = normalize(mul(float3(0.0f, 1.0f, 0.0f), (float3x3) world));
    output.uv = input.uv;

    return output;
}

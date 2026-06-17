#include "../common/Water.hlsli"

float randomValue(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898f, 78.233f))) * 43758.5453123f);
}

float valueNoise(float2 st)
{
    float2 i = floor(st);
    float2 f = frac(st);

    float a = randomValue(i);
    float b = randomValue(i + float2(1.0f, 0.0f));
    float c = randomValue(i + float2(0.0f, 1.0f));
    float d = randomValue(i + float2(1.0f, 1.0f));

    float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0f - u.x) + (d - b) * u.x * u.y;
}

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 localPos = input.pos;

    float topMask = smoothstep(0.62f, 0.92f, saturate(input.normal.y * 0.5f + 0.5f));
    float speed = max(waveSpeed, 0.05f);
    float height = max(waveHeight, 0.0f);
    float detail = max(waveFrequency, 0.15f);
    float viscosity = max(effectScale, 0.1f);

    float2 worldXZ = mul(localPos, world).xz;
    float2 flow = float2(flowSpeedX, flowSpeedY);
    float t = time * speed / (0.8f + viscosity * 0.65f);
    float2 coord = worldXZ * (0.04f * detail / (0.85f + viscosity * 0.4f)) + flow * t * 0.045f;
    float heave = valueNoise(coord + float2(t * 0.03f, -t * 0.018f)) - 0.5f;

    localPos.y += heave * height * 0.14f * topMask;

    output.pos = mul(localPos, WVP);
    output.worldPos = mul(localPos, world).xyz;
    output.screenPos = output.pos;
    output.localPos = localPos.xyz;

    output.normal = normalize(mul(input.normal, (float3x3) world));
    output.uv = input.uv;

    return output;
}

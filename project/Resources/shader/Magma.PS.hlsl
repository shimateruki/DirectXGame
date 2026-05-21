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
    float b = random(i + float2(1.0f, 0.0f));
    float c = random(i + float2(0.0f, 1.0f));
    float d = random(i + float2(1.0f, 1.0f));
    float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(a, b, u.x) + (c - a) * u.y * (1.0f - u.x) + (d - b) * u.x * u.y;
}

float4 main(VSOutput input) : SV_TARGET
{
    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 flatNormal = abs(normalize(cross(dx, dy)));

    float2 baseUV;
    if (flatNormal.y > 0.5f) {
        baseUV = input.worldPos.xz * 0.4f + float2(uvOffsetX, uvOffsetY);
    } else if (flatNormal.x > 0.5f) {
        baseUV = input.worldPos.zy * 0.4f + float2(uvOffsetY, -time * 0.05f);
    } else {
        baseUV = input.worldPos.xy * 0.4f + float2(uvOffsetX, -time * 0.05f);
    }

    float n1 = noise(baseUV + float2(time * 0.02f, time * 0.01f));
    float n2 = noise(baseUV * 1.8f - float2(time * 0.01f, time * 0.03f));
    float magmaNoise = (n1 + n2) * 0.5f;

    float3 darkCrust = color.rgb * 0.1f;
    float3 redLava = color.rgb;
    float3 yellowHot = float3(1.5f, 1.2f, 0.0f);

    float3 finalColor = darkCrust;
    finalColor = lerp(finalColor, redLava, smoothstep(0.4f, 0.6f, magmaNoise));
    finalColor = lerp(finalColor, yellowHot, smoothstep(0.7f, 0.8f, magmaNoise));
    return float4(finalColor, 1.0f);
}

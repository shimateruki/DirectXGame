#include "Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
SamplerState smp : register(s0);

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

float LinearizeDepth(float z)
{
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    return (nearClip * farClip) / (farClip - z * (farClip - nearClip));
}

float4 main(VSOutput input) : SV_TARGET
{
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV.x = screenUV.x * 0.5f + 0.5f;
    screenUV.y = -screenUV.y * 0.5f + 0.5f;

    float bgDepthZ = depthTex.Sample(smp, screenUV).r;
    float bgLinearDepth = LinearizeDepth(bgDepthZ);
    float waterLinearDepth = LinearizeDepth(input.screenPos.z / input.screenPos.w);
    float depthDiff = max(bgLinearDepth - waterLinearDepth, 0.0f);
    float waterDepthFactor = saturate(depthDiff / 3.0f);

    float2 flowOffset = float2(flowSpeedX, flowSpeedY) * time;
    float2 noiseUV = (input.worldPos.xz + flowOffset) * 1.5f;

    float n1 = noise(noiseUV + float2(time * 0.2f, time * 0.1f));
    float n2 = noise(noiseUV * 1.5f - float2(time * 0.1f, time * 0.2f));
    float waveNoise = (n1 + n2) * 0.5f;

    float3 lightDir = normalize(float3(1.0f, -1.0f, 1.0f));
    float3 normal = normalize(input.normal);
    float3 detailNormal = normalize(normal + float3(waveNoise * 0.15f, 0.0f, waveNoise * 0.15f));

    float diffuse = max(dot(detailNormal, -lightDir), 0.0f) * 0.5f + 0.5f;
    float3 viewDir = normalize(float3(0.0f, 0.5f, -1.0f));
    float3 lightReflectDir = reflect(lightDir, detailNormal);
    float specular = pow(max(dot(viewDir, lightReflectDir), 0.0f), 60.0f) * waveNoise;
    float3 viewReflectDir = reflect(-viewDir, detailNormal);

    float skyFactor = smoothstep(0.0f, 1.0f, viewReflectDir.y);
    float3 skyColor = lerp(float3(0.7f, 0.85f, 1.0f), float3(0.1f, 0.4f, 0.8f), skyFactor);
    float fresnel = pow(1.0f - max(dot(viewDir, detailNormal), 0.0f), 3.0f);

    float2 distortedUV = screenUV + detailNormal.xz * 0.05f;
    float3 refractionColor = grabTex.Sample(smp, distortedUV).rgb;

    float2 causticUV = input.worldPos.xz * 2.5f;
    float c1 = noise(causticUV + float2(time * 1.2f, time * 0.8f));
    float c2 = noise(causticUV * 1.5f - float2(time * 0.5f, time * 1.1f));
    float causticPattern = pow(max(1.0f - abs(c1 - c2), 0.0f), 8.0f);
    float causticFade = 1.0f - saturate(depthDiff / 15.0f);
    refractionColor += float3(1.0f, 1.0f, 0.8f) * causticPattern * causticFade * 1.5f;

    float foamMask = 1.0f - saturate(depthDiff / 0.8f);
    float foam = step(0.6f, waveNoise) * foamMask;

    float4 finalColor = color;
    finalColor.rgb = lerp(refractionColor, finalColor.rgb, waterDepthFactor);
    finalColor.a = lerp(0.0f, 1.0f, saturate(waterDepthFactor * 4.0f));
    finalColor.rgb = (finalColor.rgb * diffuse) + (float3(1.0f, 1.0f, 1.0f) * specular * 1.5f);
    finalColor.rgb = lerp(finalColor.rgb, skyColor, fresnel * 0.8f);
    finalColor.rgb += float3(1.0f, 1.0f, 1.0f) * foam;
    finalColor.a = min(finalColor.a + fresnel * 0.5f + foam, 1.0f);

    return finalColor;
}

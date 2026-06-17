#include "../common/Water.hlsli"

Texture2D<float> depthTex : register(t0);
Texture2D<float4> grabTex : register(t1);
Texture2D<float4> bakedWaterFoamTex : register(t2);
Texture2D<float4> bakedWaterFlowTex : register(t3);
SamplerState smp : register(s0);

float random(float2 st)
{
    return frac(sin(dot(st.xy, float2(12.9898f, 78.233f))) * 43758.5453123f);
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

float fbm(float2 p)
{
    float value = 0.0f;
    float amplitude = 0.5f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        value += noise(p) * amplitude;
        p = p * 2.03f + float2(17.13f, 11.71f);
        amplitude *= 0.5f;
    }

    return value;
}

float2 rotate2D(float2 p, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

float LinearizeDepth(float z)
{
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    return (nearClip * farClip) / (farClip - z * (farClip - nearClip));
}

float3 BuildStableWaterNormal(float3 worldPos, float3 baseNormal, float n1, float n2, float waveNoise)
{
    float detail = max(waveFrequency, 0.1f);
    float speed = max(waveSpeed, 0.02f);
    float2 flow = float2(flowSpeedX, flowSpeedY);
    float2 waveCoord = worldPos.xz * detail * 0.18f + flow * time * 0.35f;

    float phaseA = waveCoord.x * 2.3f + waveCoord.y * 0.6f + time * speed;
    float phaseB = rotate2D(waveCoord, 1.1f).x * 2.0f - time * speed * 0.73f;
    float phaseC = waveCoord.x * 0.7f + waveCoord.y * 1.7f + time * speed * 1.31f;
    float normalStrength = saturate(abs(waveHeight) * 0.45f + 0.18f);

    float slopeX = cos(phaseA) * 0.12f + sin(phaseB) * 0.09f + (waveNoise - 0.5f) * 0.10f;
    float slopeZ = -sin(phaseB) * 0.11f + cos(phaseC) * 0.08f + (n2 - n1) * 0.16f;
    float3 proceduralNormal = normalize(float3(-slopeX * normalStrength, 1.0f, -slopeZ * normalStrength));

    return normalize(lerp(baseNormal, proceduralNormal, 0.95f));
}

float BuildWaterLine(float2 coord, float speed, float width)
{
    float wave = sin(coord.x * 4.2f + coord.y * 1.4f + time * speed);
    float band = 1.0f - abs(wave);
    return smoothstep(1.0f - width, 1.0f, band);
}

float BuildCaustics(float2 worldXZ)
{
    float2 causticCoordA = rotate2D(worldXZ * 1.7f, 0.45f) + float2(time * 0.42f, time * 0.27f);
    float2 causticCoordB = rotate2D(worldXZ * 2.4f, -0.8f) - float2(time * 0.24f, time * 0.39f);
    float c1 = fbm(causticCoordA);
    float c2 = fbm(causticCoordB);
    float causticLine = 1.0f - abs(c1 - c2);
    return pow(saturate(causticLine), 7.5f);
}

float4 main(VSOutput input) : SV_TARGET
{
    clip(input.localPos.z - 0.5f);

    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV.x = screenUV.x * 0.5f + 0.5f;
    screenUV.y = -screenUV.y * 0.5f + 0.5f;
    screenUV = saturate(screenUV);

    float bgDepthZ = depthTex.Sample(smp, screenUV).r;
    float bgLinearDepth = LinearizeDepth(bgDepthZ);
    float waterLinearDepth = LinearizeDepth(input.screenPos.z / input.screenPos.w);
    float depthDiff = max(bgLinearDepth - waterLinearDepth, 0.0f);
    float waterDepthFactor = saturate(depthDiff / 6.8f);

    float2 flow = float2(flowSpeedX, flowSpeedY);
    float2 bakedFlowUV = frac(input.worldPos.xz * 0.045f + flow * time * 0.035f + float2(uvOffsetX, uvOffsetY) * 0.08f);
    float4 bakedFlow = bakedWaterFlowTex.Sample(smp, bakedFlowUV);
    float2 bakedFlowVector = (bakedFlow.rg * 2.0f - 1.0f) * 0.18f;

    float2 noiseUV = input.worldPos.xz * 0.85f + flow * time * 0.55f + uvOffsetX.xx + bakedFlowVector;

    float n1 = fbm(noiseUV + float2(time * 0.10f, time * 0.04f));
    float n2 = fbm(noiseUV * 1.8f - float2(time * 0.08f, time * 0.13f));
    float waveNoise = saturate(n1 * 0.58f + n2 * 0.42f);

    float3 lightDir = normalize(float3(1.0f, -1.0f, 1.0f));
    float3 baseNormal = normalize(input.normal + float3(0.0f, 0.0001f, 0.0f));
    float3 detailNormal = BuildStableWaterNormal(input.worldPos, baseNormal, n1, n2, waveNoise);

    float diffuse = max(dot(detailNormal, -lightDir), 0.0f) * 0.32f + 0.68f;
    float3 viewToCamera = cameraWorldPosition - input.worldPos;
    float3 viewDir = viewToCamera * rsqrt(max(dot(viewToCamera, viewToCamera), 0.0001f));

    float3 lightReflectDir = reflect(lightDir, detailNormal);
    float specularCore = pow(max(dot(viewDir, lightReflectDir), 0.0f), 88.0f);
    float specularWide = pow(max(dot(viewDir, lightReflectDir), 0.0f), 18.0f) * 0.18f;
    float specular = (specularCore + specularWide) * (0.45f + waveNoise * 0.75f);

    float3 viewReflectDir = reflect(-viewDir, detailNormal);
    float skyFactor = smoothstep(0.0f, 1.0f, viewReflectDir.y);
    float3 skyColor = lerp(float3(0.62f, 0.86f, 1.0f), float3(0.12f, 0.42f, 0.78f), skyFactor);

    float fresnel = pow(1.0f - max(dot(viewDir, detailNormal), 0.0f), 2.8f);

    float refractionStrength = lerp(0.010f, 0.060f, saturate(effectScale * 0.5f));
    float2 distortedUV = saturate(screenUV + detailNormal.xz * refractionStrength * (0.45f + waterDepthFactor * 0.55f));
    float3 refractionColor = grabTex.Sample(smp, distortedUV).rgb;

    float tintMax = max(color.r, max(color.g, color.b));
    float tintMin = min(color.r, min(color.g, color.b));
    float tintLuminance = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
    float customTint = saturate((tintMax - tintMin) * 2.2f + (1.0f - tintLuminance) * 0.25f);
    float3 shallowTint = lerp(float3(0.55f, 0.88f, 0.98f), color.rgb, customTint * 0.50f);
    float3 midTint = lerp(float3(0.20f, 0.58f, 0.82f), color.rgb * 0.90f, customTint * 0.42f);
    float3 deepTint = lerp(float3(0.06f, 0.28f, 0.58f), color.rgb * 0.72f, customTint * 0.34f);
    float3 waterTint = lerp(lerp(shallowTint, midTint, smoothstep(0.0f, 0.48f, waterDepthFactor)), deepTint, smoothstep(0.35f, 1.0f, waterDepthFactor));

    float2 bakedFoamUV = frac(input.worldPos.xz * 0.070f - flow.yx * time * 0.030f + bakedFlowVector * 0.35f);
    float4 bakedFoam = bakedWaterFoamTex.Sample(smp, bakedFoamUV);

    float caustics = BuildCaustics(input.worldPos.xz);
    caustics = saturate(caustics * 0.70f + pow(saturate(bakedFoam.g), 2.0f) * 0.34f);
    float causticFade = (1.0f - saturate(depthDiff / 18.0f)) * saturate(depthDiff / 0.35f);
    float causticStrength = caustics * causticFade * lerp(0.28f, 0.62f, saturate(effectIntensity * 0.45f));

    float foamWidth = lerp(0.42f, 2.10f, saturate(effectSoftness));
    float contactMask = (1.0f - smoothstep(0.0f, foamWidth, depthDiff)) * smoothstep(0.045f, 0.24f, depthDiff);
    float lineA = BuildWaterLine(input.worldPos.xz * max(waveFrequency, 0.1f) * 0.16f + flow * time * 0.20f, waveSpeed * 0.62f, 0.16f);
    float lineB = BuildWaterLine(rotate2D(input.worldPos.xz, -0.85f) * max(waveFrequency, 0.1f) * 0.12f - flow.yx * time * 0.18f, waveSpeed * 0.47f, 0.12f);
    float bakedSurface = bakedWaterFoamTex.Sample(smp, frac(input.worldPos.xz * 0.105f + flow * time * 0.020f)).r;
    float broadWaveA = sin(dot(input.worldPos.xz, float2(0.072f, 0.034f)) + time * waveSpeed * 0.42f);
    float broadWaveB = sin(dot(input.worldPos.xz, float2(-0.046f, 0.081f)) - time * waveSpeed * 0.31f);
    float broadWave = saturate((broadWaveA + broadWaveB) * 0.25f + 0.5f);
    float surfaceLines = saturate(lineA * 0.55f + lineB * 0.35f + bakedSurface * 0.18f + broadWave * 0.10f) * smoothstep(0.26f, 0.80f, waveNoise);
    float crest = smoothstep(0.62f, 0.94f, broadWave) * smoothstep(0.38f, 0.88f, waveNoise);
    float contactNoise = saturate(surfaceLines * 0.45f + bakedFoam.b * 0.24f + broadWave * 0.16f);
    float edgeFoam = contactMask * contactNoise * lerp(0.18f, 0.34f, saturate(effectIntensity));
    float openFoam = surfaceLines * lerp(0.12f, 0.20f, waterDepthFactor);
    openFoam += bakedFoam.a * 0.08f * smoothstep(0.16f, 0.74f, waterDepthFactor);
    float foam = saturate(edgeFoam + openFoam + crest * 0.08f);

    float3 body = lerp(refractionColor, waterTint, lerp(0.28f, 0.72f, waterDepthFactor));
    body *= diffuse;
    float reflectionAmount = saturate(fresnel * 0.52f + (1.0f - waterDepthFactor) * 0.10f);
    body = lerp(body, skyColor, reflectionAmount);
    float3 contactTint = lerp(waterTint, float3(0.74f, 0.92f, 1.0f), 0.34f);
    body = lerp(body, contactTint, contactMask * 0.18f);
    body += float3(0.75f, 0.95f, 1.0f) * surfaceLines * 0.15f;
    body += float3(0.32f, 0.64f, 0.86f) * (broadWave - 0.5f) * 0.075f;
    body += float3(0.52f, 0.84f, 1.0f) * crest * 0.065f;
    body += float3(1.0f, 0.96f, 0.72f) * causticStrength;
    body += float3(1.0f, 1.0f, 1.0f) * specular * max(effectIntensity, 0.25f) * 0.95f;
    body = lerp(body, float3(0.92f, 0.98f, 1.0f), foam * 0.30f);
    body *= lerp(0.92f, 1.24f, saturate(effectIntensity * 0.55f));

    float artistAlpha = max(color.a, 0.62f);
    float alpha = (0.42f + waterDepthFactor * 0.28f + fresnel * 0.18f + foam * 0.16f) * artistAlpha;
    return float4(saturate(body), saturate(alpha));
}

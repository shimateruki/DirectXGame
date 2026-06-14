#include "Object3d.hlsli"

static const int kMaxPointLights = 100;
static const int kMaxSpotLights = 100;

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t3 padding1;
    float32_t4x4 uvTransform;
    int32_t selectedLighting;
    float32_t shininess;
    int32_t materialType;
    float32_t roughness;
    float32_t metallic;
    
    int32_t enableNormalMap;
    int32_t enableEnvMap;
    float envIntensity;
    float emissive;
    float time;
    float32_t2 padding2;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intenssity;
    float32_t3 ambientColor;
    float fogStart;
    float fogEnd;
    float32_t3 fogColor;
    float fogHeightMin; // 霧が最も濃い（100%溜まっている）高さ
    float fogHeightMax; // 霧が完全に晴れる高さ
    float volumetricIntensity;
    int volumetricSteps;
    int enableFog;
    float3 padding3;
    float32_t4x4 lightViewProj;
};

struct Camera
{
    float32_t3 worldPosition;
};

struct PointLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float radius;
    float decay;
    float32_t2 padding;
};

struct PointLightConstData
{
    PointLight lights[kMaxPointLights];
    int activeCount;
    float32_t3 padding;
};

struct SpotLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float32_t3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    float32_t padding;
};

struct SpotLightConstData
{
    SpotLight lights[kMaxSpotLights];
    int activeCount;
    float32_t3 padding;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLightConstData> gPointLights : register(b3);
ConstantBuffer<SpotLightConstData> gSpotLights : register(b4);

Texture2D<float32_t4> gTexture : register(t0);
TextureCube<float32_t4> gEnvTexture : register(t2);
SamplerState gSampler : register(s0);
Texture2D<float32_t4> gNormalMap : register(t3);
Texture2D<float32_t4> gOrmMap : register(t4);
Texture2D<float32_t> gShadowMap : register(t5);
SamplerState gShadowSampler : register(s1);

static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;
    return num / max(denom, 0.0000001f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    float num = NdotV;
    float denom = NdotV * (1.0f - k) + k;
    return num / max(denom, 0.0000001f);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

float3 CalcPBRLight(float3 L, float3 V, float3 N, float3 radiance, float3 albedo, float roughness, float metallic, float3 F0)
{
    float3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
    float3 numerator = NDF * G * F;
    float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.0001f;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    kD *= 1.0f - metallic;

    float NdotL = max(dot(N, L), 0.0f);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float ValueNoise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);

    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float3 BuildStylizedTerrainColor(float3 textureBase, float3 terrainTint, float3 N, float3 V, float3 worldPosition, float shadowFactor)
{
    float textureBlend = lerp(0.10f, 0.58f, saturate(gMaterial.roughness));
    float patchStrength = lerp(0.10f, 0.55f, saturate(gMaterial.metallic));

    float luminance = dot(textureBase, float3(0.299f, 0.587f, 0.114f));
    float textureValue = lerp(0.72f, 1.22f, smoothstep(0.08f, 0.98f, luminance));
    float3 softTexture = lerp(float3(luminance, luminance, luminance), textureBase, 0.54f);
    softTexture = saturate(softTexture * 1.06f + 0.015f);

    float slope = saturate(N.y);
    float macroNoise = ValueNoise2D(worldPosition.xz * 0.035f);
    float detailNoise = ValueNoise2D(worldPosition.xz * 0.16f + macroNoise * 3.25f);
    float patchNoise = floor((macroNoise * 0.65f + detailNoise * 0.35f) * 4.0f) / 3.0f;

    float3 grassShadow = float3(0.20f, 0.36f, 0.18f);
    float3 grassBase = float3(0.42f, 0.62f, 0.28f);
    float3 grassLight = float3(0.66f, 0.76f, 0.40f);
    float3 earthBase = float3(0.43f, 0.33f, 0.20f);
    float3 rockBase = float3(0.45f, 0.46f, 0.40f);

    float steepMask = 1.0f - smoothstep(0.18f, 0.55f, slope);
    float dryMask = smoothstep(0.72f, 0.95f, macroNoise) * slope;
    float3 palette = lerp(earthBase, grassBase, slope);
    palette = lerp(palette, rockBase, steepMask * 0.65f);
    palette = lerp(palette, grassLight, dryMask * 0.28f);
    palette = lerp(palette, grassShadow, (1.0f - patchNoise) * patchStrength * 0.30f);

    float3 textureDetail = palette * textureValue;
    textureDetail = lerp(textureDetail, softTexture * palette * 1.35f, textureBlend * 0.36f);

    float3 baseColor = lerp(palette, textureDetail, textureBlend);
    baseColor *= lerp(0.76f, 1.16f, patchNoise);

    float3 tint = max(saturate(terrainTint), float3(0.035f, 0.035f, 0.035f));
    float tintDelta = max(max(abs(tint.r - 1.0f), abs(tint.g - 1.0f)), abs(tint.b - 1.0f));
    float tintWeight = saturate(tintDelta * 1.65f);
    baseColor = lerp(baseColor, baseColor * tint * 1.18f, tintWeight);

    float baseLuma = dot(baseColor, float3(0.299f, 0.587f, 0.114f));
    baseColor = lerp(float3(baseLuma, baseLuma, baseLuma), baseColor, 1.34f);
    baseColor = saturate(baseColor * 1.08f + 0.012f);
    baseColor = min(baseColor, float3(0.94f, 0.94f, 0.94f));

    float3 L = normalize(-gDirectionalLight.direction);
    float NdotL = saturate(dot(N, L));
    float lightBand = (NdotL < 0.34f) ? 0.50f : ((NdotL < 0.70f) ? 0.76f : 1.04f);
    lightBand *= lerp(0.50f, 1.0f, shadowFactor);

    float ambientLevel = max(max(gDirectionalLight.ambientColor.r, gDirectionalLight.ambientColor.g), gDirectionalLight.ambientColor.b);
    float ambientStrength = 0.27f + saturate(ambientLevel) * 0.18f;
    float directStrength = saturate(gDirectionalLight.intenssity * 0.78f);
    float3 lightTint = lerp(float3(1.0f, 1.0f, 1.0f), saturate(gDirectionalLight.color.rgb), 0.22f);
    float combinedLight = saturate(ambientStrength + lightBand * directStrength * 0.62f);
    float3 color = baseColor * combinedLight * lightTint;
    float3 localLight = float3(0.0f, 0.0f, 0.0f);

    for (int i = 0; i < gPointLights.activeCount; ++i)
    {
        PointLight pLight = gPointLights.lights[i];
        float3 Lp = pLight.position - worldPosition;
        float distance = length(Lp);
        Lp = normalize(Lp);
        float attenuation = pow(saturate(-distance / pLight.radius + 1.0f), pLight.decay);
        float pointBand = (dot(N, Lp) > 0.18f) ? 1.0f : 0.38f;
        localLight += baseColor * pLight.color.rgb * min(pLight.intensity, 2.0f) * attenuation * pointBand * 0.16f;
    }

    for (int j = 0; j < gSpotLights.activeCount; ++j)
    {
        SpotLight sLight = gSpotLights.lights[j];
        float3 Ls = sLight.position - worldPosition;
        float distance = length(Ls);
        Ls = normalize(Ls);
        float distanceFactor = pow(saturate(-distance / sLight.distance + 1.0f), sLight.decay);
        float angleCos = dot(-Ls, normalize(sLight.direction));
        float falloffFactor = saturate((angleCos - sLight.cosAngle) / (sLight.cosFalloffStart - sLight.cosAngle));
        float spotBand = (dot(N, Ls) > 0.18f) ? 1.0f : 0.38f;
        localLight += baseColor * sLight.color.rgb * min(sLight.intensity, 2.0f) * distanceFactor * falloffFactor * spotBand * 0.16f;
    }

    float rim = pow(1.0f - saturate(dot(N, V)), 3.0f) * slope * 0.045f;
    float3 rimColor = grassLight * rim;

    return saturate((color + localLight + rimColor) * 1.04f);
}

float SmoothBox(float2 p, float2 halfSize, float softness)
{
    float2 d = abs(p) - halfSize;
    float outside = max(d.x, d.y);
    return 1.0f - smoothstep(0.0f, softness, outside);
}

float BuildDashArrow(float2 uv, float time, float speed, float density)
{
    float cellY = frac(uv.y * density - time * speed);
    float2 p = float2(uv.x - 0.5f, cellY - 0.5f);

    float shaft = SmoothBox(p - float2(0.0f, -0.15f), float2(0.055f, 0.22f), 0.035f);

    float headY = p.y - 0.05f;
    float halfWidth = max(0.0f, (0.36f - headY) * 0.76f);
    float headSide = 1.0f - smoothstep(0.0f, 0.035f, abs(p.x) - halfWidth);
    float headVertical = smoothstep(0.02f, 0.10f, headY) * (1.0f - smoothstep(0.30f, 0.38f, headY));
    float head = headSide * headVertical;

    return saturate(max(shaft, head));
}

float3 BuildDashPanelColor(float2 uv, float3 textureBase, float3 normal, float shadowFactor)
{
    float2 panelUv = frac(uv);
    float speed = lerp(0.9f, 3.8f, saturate(gMaterial.roughness));
    float density = lerp(1.35f, 3.2f, saturate(gMaterial.metallic));

    float topMask = smoothstep(0.18f, 0.58f, abs(normal.y));
    float lane = 1.0f - smoothstep(0.36f, 0.50f, abs(panelUv.x - 0.5f));
    float edgeDistance = min(min(panelUv.x, 1.0f - panelUv.x), min(panelUv.y, 1.0f - panelUv.y));
    float border = 1.0f - smoothstep(0.025f, 0.075f, edgeDistance);

    float arrow = BuildDashArrow(panelUv, gMaterial.time, speed, density);
    float trailA = pow(saturate(0.5f + 0.5f * sin((uv.y - gMaterial.time * speed) * 34.0f + uv.x * 7.0f)), 7.0f) * lane;
    float trailB = pow(saturate(0.5f + 0.5f * sin((uv.y - gMaterial.time * (speed * 1.25f)) * 19.0f - uv.x * 11.0f)), 10.0f) * lane;
    float sideRail = smoothstep(0.34f, 0.43f, abs(panelUv.x - 0.5f)) * (1.0f - smoothstep(0.45f, 0.50f, abs(panelUv.x - 0.5f)));
    float pulse = 0.82f + 0.18f * sin(gMaterial.time * 5.5f);

    float3 base = lerp(float3(0.018f, 0.12f, 0.16f), float3(0.035f, 0.38f, 0.46f), lane);
    base *= lerp(0.82f, 1.12f, dot(saturate(textureBase), float3(0.299f, 0.587f, 0.114f)));

    float3 accent = lerp(float3(0.08f, 0.95f, 1.0f), saturate(gMaterial.color.rgb * 1.35f), 0.35f);
    float glow = arrow * 2.45f + trailA * 0.72f + trailB * 0.45f + sideRail * 0.75f + border * 0.55f;
    float3 topColor = base + accent * glow * pulse;
    topColor *= lerp(0.72f, 1.0f, shadowFactor);

    float3 sideColor = float3(0.025f, 0.08f, 0.10f) * lerp(0.75f, 1.15f, textureBase);
    return lerp(sideColor, topColor, topMask) * max(1.0f, gMaterial.emissive);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    if (gMaterial.materialType == 0 && textureColor.a <= 0.5)
    {
        discard;
    }
    
    float shadowFactor = 1.0f;

    // W除算 (同次座標系からデカルト座標系へ)
    float3 shadowPos = input.shadowPosition.xyz / input.shadowPosition.w;
    
    // クリップ空間(-1 ～ 1) を UV空間(0 ～ 1) に変換
    float2 shadowUV = float2(
        (shadowPos.x + 1.0f) / 2.0f,
        (1.0f - shadowPos.y) / 2.0f // Yは上下反転
    );

    // 画面外を真っ黒にしないための範囲チェック
    if (shadowPos.z > 0.0f && shadowPos.z < 1.0f &&
        shadowUV.x > 0.0f && shadowUV.x < 1.0f &&
        shadowUV.y > 0.0f && shadowUV.y < 1.0f)
    {
        float bias = 0.005f;
        shadowFactor = 0.0f;

        float2 texelSize = 1.0f / 2048.0f;
        float spread = 1.5f;

        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                // spread を掛けて、サンプリングする範囲を広げる
                float2 offset = float2(x, y) * texelSize * spread;
                float depthFromLight = gShadowMap.Sample(gShadowSampler, shadowUV + offset);
                
                if (shadowPos.z - bias <= depthFromLight)
                {
                    shadowFactor += 1.0f;
                }
            }
        }
        shadowFactor /= 9.0f;
    }
    
    float NdotL;
    float cos;
    
    switch (gMaterial.selectedLighting)
    {
        case 0: // None
            output.color = gMaterial.color * textureColor;
            break;

        case 1: // Lambert (Directional Only)
            cos = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));
            output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intenssity;
            output.color.a = gMaterial.color.a * textureColor.a;
            break;

        case 2: // PBR (Cook-Torrance BRDF)
        {   
                float3 N = normalize(input.normal);
                float3 V = normalize(gCamera.worldPosition - input.worldPosition);

            // ===========================================================
            // ガラスシェーダー
            // ===========================================================
                if (gMaterial.materialType == 1)
                {
                    float NdotV = saturate(dot(N, V));
                    float iorRatio = 1.0f / 1.52f;
                    float3 IOR_RGB = float3(iorRatio * 0.99f, iorRatio, iorRatio * 1.01f);

                    float3 RefractR = refract(-V, N, IOR_RGB.r);
                    float3 RefractG = refract(-V, N, IOR_RGB.g);
                    float3 RefractB = refract(-V, N, IOR_RGB.b);

                    float3 envR = gEnvTexture.SampleLevel(gSampler, RefractR, 0.0f).rgb;
                    float3 envG = gEnvTexture.SampleLevel(gSampler, RefractG, 0.0f).rgb;
                    float3 envB = gEnvTexture.SampleLevel(gSampler, RefractB, 0.0f).rgb;
                
                    float3 refractionColor = float3(envR.r, envG.g, envB.b) * gMaterial.envIntensity;
                
                    float F0_glass = 0.04f;
                    float fresnel = F0_glass + (1.0f - F0_glass) * pow(1.0f - NdotV, 5.0f);
                    float darkRim = smoothstep(0.6f, 1.0f, 1.0f - pow(NdotV, 0.5f));

                    float3 ReflectVec = reflect(-V, N);
                    float3 reflectionColor = gEnvTexture.SampleLevel(gSampler, ReflectVec, 0.0f).rgb * gMaterial.envIntensity;

                    float3 L_Dir = normalize(-gDirectionalLight.direction);
                    float3 H_glass = normalize(L_Dir + V);
                    float NdotH_glass = saturate(dot(N, H_glass));

                    float specPowerPrimary = 8192.0f;
                    float3 specPrimary = float3(1.0f, 1.0f, 1.0f) * pow(NdotH_glass, specPowerPrimary) * 5.0f;

                    float3 N_Back = normalize(N + V * 0.2f);
                    float NdotH_Back = saturate(dot(N_Back, H_glass));
                    float specPowerSecondary = 512.0f;
                    float3 specSecondary = float3(1.0f, 1.0f, 1.0f) * pow(NdotH_Back, specPowerSecondary) * 1.0f;
                    float3 totalSpecular = specPrimary + specSecondary;

                    float internalFocus = saturate(dot(N, -L_Dir));
                    float caustic = smoothstep(0.9f, 1.0f, internalFocus);
                    float3 fakeCaustics = float3(1.0f, 0.9f, 0.7f) * caustic * 2.0f;

                    float3 bodyColor = lerp(refractionColor * (1.0f - darkRim * 0.8f), reflectionColor, fresnel);
                    output.color.rgb = bodyColor + totalSpecular + fakeCaustics;

                    float alphaBase = 0.02f;
                    output.color.a = saturate(alphaBase + fresnel + caustic * 0.5f + (totalSpecular.r * 0.5f));
                }
            // ===========================================================
            // 氷シェーダー
            // ===========================================================
                else if (gMaterial.materialType == 2)
                {
                    float NdotV = saturate(dot(N, V));
                    float iorRatio = 1.0f / 1.31f;
                    float3 IOR_RGB = float3(iorRatio * 0.96f, iorRatio, iorRatio * 1.04f);

                    float3 RefractR = refract(-V, N, IOR_RGB.r);
                    float3 RefractG = refract(-V, N, IOR_RGB.g);
                    float3 RefractB = refract(-V, N, IOR_RGB.b);

                    float3 envR = gEnvTexture.SampleLevel(gSampler, RefractR, 0.0f).rgb;
                    float3 envG = gEnvTexture.SampleLevel(gSampler, RefractG, 0.0f).rgb;
                    float3 envB = gEnvTexture.SampleLevel(gSampler, RefractB, 0.0f).rgb;
                
                    float3 refractionColor = float3(envR.r, envG.g, envB.b) * gMaterial.envIntensity;
                
                    float3 iceBaseColor = gMaterial.color.rgb * textureColor.rgb;
                    refractionColor = lerp(refractionColor, iceBaseColor, 0.4f);

                    float F0_ice = 0.02f;
                    float fresnel = F0_ice + (1.0f - F0_ice) * pow(1.0f - NdotV, 5.0f);
                    float3 ReflectVec = reflect(-V, N);
                
                    float3 reflectionColor = gEnvTexture.SampleLevel(gSampler, ReflectVec, 0.0f).rgb * gMaterial.envIntensity;

                    float3 L_Dir = normalize(-gDirectionalLight.direction);
                    float3 H = normalize(L_Dir + V);
                    float3 specular = float3(1.0f, 1.0f, 1.0f) * pow(saturate(dot(N, H)), 1024.0f) * 2.0f;

                    float3 bodyColor = lerp(refractionColor, reflectionColor, fresnel);
                    output.color.rgb = bodyColor + specular;
                    output.color.a = saturate(0.5f + fresnel + (specular.r * 0.5f));
                }
            // ===========================================================
            // ホログラム・バリア
            // ===========================================================
                else if (gMaterial.materialType == 3)
                {
                    float NdotV = saturate(dot(N, V));
                    float rimLight = pow(1.0f - NdotV, 3.0f);
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                    float3 emission = baseColor * rimLight * 3.0f;
                    float3 frontColor = baseColor * 0.2f;
                
                    output.color.rgb = emission + frontColor;
                    output.color.a = saturate(rimLight * 2.0f + 0.1f) * gMaterial.color.a * textureColor.a;
                }
            // ===========================================================
            // 消滅 (Dissolve)
            // ===========================================================
                else if (gMaterial.materialType == 4)
                {
                    float progress = saturate(1.0f - gMaterial.color.a);
                    float2 flowUv = input.worldPosition.xz * 2.35f;
                    flowUv += float2(gMaterial.time * 0.55f, -gMaterial.time * 0.37f);
                    float largeNoise = ValueNoise2D(flowUv);
                    float fineNoise = Hash21(flowUv * 5.7f + input.texcoord * 11.0f);
                    float dissolveNoise = saturate(largeNoise * 0.70f + fineNoise * 0.30f);
                    float threshold = saturate(gMaterial.color.a);

                    if (dissolveNoise > threshold)
                    {
                        discard;
                    }
                
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                    float3 L = normalize(-gDirectionalLight.direction);
                    float direct = saturate(dot(N, L));
                    float3 litBase = baseColor * (gDirectionalLight.ambientColor + gDirectionalLight.color.rgb * direct * gDirectionalLight.intenssity);

                    float edgeWidth = lerp(0.055f, 0.16f, progress);
                    float edge = 1.0f - smoothstep(0.0f, edgeWidth, threshold - dissolveNoise);
                    float flash = smoothstep(0.02f, 0.18f, progress) * (1.0f - smoothstep(0.58f, 0.92f, progress));
                    float ring = pow(saturate(0.5f + 0.5f * sin((input.worldPosition.y * 7.0f) + gMaterial.time * 18.0f)), 5.0f) * (1.0f - progress);
                    float3 hotWhite = float3(1.95f, 1.95f, 1.72f);
                    float3 goldEdge = float3(2.5f, 1.12f, 0.22f);
                    float3 glow = lerp(hotWhite, goldEdge, saturate(progress * 1.25f));

                    output.color.rgb = litBase;
                    output.color.rgb = lerp(output.color.rgb, hotWhite, flash * 0.55f);
                    output.color.rgb += glow * edge * 2.8f;
                    output.color.rgb += goldEdge * ring * edge * 0.85f;
                    output.color.rgb *= max(1.0f, gMaterial.emissive * 0.72f);
                    output.color.a = 1.0f;
                }
            // ===========================================================
            // マグマ・覚醒 (Emissive)
            // ===========================================================
                else if (gMaterial.materialType == 5)
                {
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                
                    float luminance = dot(baseColor, float3(0.299f, 0.587f, 0.114f));
                    float glowFactor = smoothstep(0.4f, 0.0f, luminance);
                    float3 glowColor = float3(2.5f, 0.8f, 0.0f) * gMaterial.color.rgb;

                    float NdotL = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));
                    float3 litColor = baseColor * gDirectionalLight.color.rgb * NdotL;
                
                    output.color.rgb = litColor + (glowColor * glowFactor);
                    output.color.a = gMaterial.color.a * textureColor.a;
                }
            // ===========================================================
            // トゥーン調 (Cel Shaded)
            // ===========================================================
                else if (gMaterial.materialType == 6)
                {
                    float3 N = normalize(input.normal);
                    float3 L = normalize(-gDirectionalLight.direction);
                    float3 V = normalize(gCamera.worldPosition - input.worldPosition);
                
                    float NdotL = dot(N, L);
                    float celFactor = (NdotL > 0.0f) ? 1.0f : 0.3f;
                
                    float NdotV = saturate(dot(N, V));
                    float outline = (NdotV < 0.25f) ? 0.0f : 1.0f;
                
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                    float3 finalColor = baseColor * gDirectionalLight.color.rgb * celFactor;
                
                    output.color.rgb = finalColor * outline;
                    output.color.a = gMaterial.color.a * textureColor.a;
                }
            // ===========================================================
            // Stylized terrain
            // ===========================================================
                else if (gMaterial.materialType == 23)
                {
                    float3 terrainNormal = normalize(input.normal);

                    if (gMaterial.enableNormalMap == 1)
                    {
                        float3 T = normalize(input.tangent);
                        T = normalize(T - dot(T, terrainNormal) * terrainNormal);
                        float3 B = cross(terrainNormal, T);
                        float3x3 TBN = float3x3(T, B, terrainNormal);
                        float3 normalMap = gNormalMap.Sample(gSampler, transformedUV.xy).rgb;
                        normalMap = normalMap * 2.0f - 1.0f;
                        float3 mappedNormal = normalize(mul(normalMap, TBN));
                        terrainNormal = normalize(lerp(terrainNormal, mappedNormal, 0.18f));
                    }

                    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
                    float3 textureBase = saturate(textureColor.rgb);
                    output.color.rgb = BuildStylizedTerrainColor(textureBase, gMaterial.color.rgb, terrainNormal, viewDir, input.worldPosition, shadowFactor);
                    output.color.a = gMaterial.color.a * textureColor.a;
                }
            // ===========================================================
            // Dash panel
            // ===========================================================
                else if (gMaterial.materialType == 24)
                {
                    float3 dashNormal = normalize(input.normal);
                    output.color.rgb = BuildDashPanelColor(transformedUV.xy, textureColor.rgb, dashNormal, shadowFactor);
                    output.color.a = gMaterial.color.a * textureColor.a;
                }
            // ===========================================================
            // Laser beam
            // ===========================================================
                else if (gMaterial.materialType == 12)
                {
                    float2 uv = input.texcoord;
                    float radial = abs(uv.x - 0.5f) * 2.0f;
                    float core = 1.0f - smoothstep(0.0f, 0.22f, radial);
                    float innerGlow = 1.0f - smoothstep(0.08f, 0.58f, radial);
                    float outerGlow = 1.0f - smoothstep(0.20f, 1.0f, radial);
                    float strandA = 0.5f + 0.5f * sin(uv.y * 54.0f + radial * 8.0f);
                    float strandB = 0.5f + 0.5f * sin(uv.y * 31.0f - radial * 13.0f);
                    float shell = smoothstep(0.34f, 0.62f, radial) * (1.0f - smoothstep(0.78f, 1.0f, radial));
                    shell *= strandA * 0.65f + strandB * 0.45f;
                    float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
                    float3 hotCore = lerp(float3(1.9f, 2.35f, 2.8f), baseColor * 2.2f, 0.38f);

                    output.color.rgb = (hotCore * core * 3.6f + baseColor * innerGlow * 2.6f + baseColor * outerGlow * 1.25f + baseColor * shell * 2.0f) * gMaterial.emissive;
                    output.color.a = saturate(gMaterial.color.a * (core + innerGlow * 0.55f + outerGlow * 0.28f + shell * 0.45f));
                }
            // ===========================================================
            // 通常のPBRマテリアル
            // ===========================================================
                else
                {
                    float3 ormColor = gOrmMap.Sample(gSampler, transformedUV.xy).rgb;
                    float roughness = gMaterial.roughness * ormColor.g;
                    float metallic = gMaterial.metallic * ormColor.b;
                
                    float3 N = normalize(input.normal);

                    if (gMaterial.enableNormalMap == 1)
                    {
                        float3 T = normalize(input.tangent);
                        T = normalize(T - dot(T, N) * N);
                        float3 B = cross(N, T);
                        float3x3 TBN = float3x3(T, B, N);
                        float3 normalMap = gNormalMap.Sample(gSampler, transformedUV.xy).rgb;
                        normalMap = normalMap * 2.0f - 1.0f;
                        N = normalize(mul(normalMap, TBN));
                    }

                    float3 albedo = gMaterial.color.rgb * textureColor.rgb;
                    float3 F0 = float3(0.04f, 0.04f, 0.04f);
                    F0 = lerp(F0, albedo, metallic);

                    float3 Lo = float3(0.0f, 0.0f, 0.0f);

                // 1. 平行光源
                    float3 L_dir = normalize(-gDirectionalLight.direction);
                
                // 【青みのある影のブレンド】
                // shadowFactorが 0.0(影) の時は青い光、1.0(日向) の時は本来の光(白) になるように補間する
                    float3 shadowTint = float3(0.15f, 0.25f, 0.5f);
                    float3 lightIntensity = lerp(shadowTint, float3(1.0f, 1.0f, 1.0f), shadowFactor);
                
                // 本来の光の強さに、青みを帯びたグラデーションを掛け合わせる
                    float3 radiance_dir = gDirectionalLight.color.rgb * gDirectionalLight.intenssity * lightIntensity;
                
                    Lo += CalcPBRLight(L_dir, V, N, radiance_dir, albedo, roughness, metallic, F0);

                // 2. 点光源
                    for (int i = 0; i < gPointLights.activeCount; ++i)
                    {
                        PointLight pLight = gPointLights.lights[i];
                        float3 L_point = pLight.position - input.worldPosition;
                        float distance = length(L_point);
                        L_point = normalize(L_point);
                        float attenuation = pow(saturate(-distance / pLight.radius + 1.0f), pLight.decay);
                        float3 radiance_point = pLight.color.rgb * pLight.intensity * attenuation;
                        Lo += CalcPBRLight(L_point, V, N, radiance_point, albedo, roughness, metallic, F0);
                    }

                // 3. スポットライト
                    for (int j = 0; j < gSpotLights.activeCount; ++j)
                    {
                        SpotLight sLight = gSpotLights.lights[j];
                        float3 L_spot = sLight.position - input.worldPosition;
                        float distance = length(L_spot);
                        L_spot = normalize(L_spot);
                        float distanceFactor = pow(saturate(-distance / sLight.distance + 1.0f), sLight.decay);
                        float angleCos = dot(-L_spot, normalize(sLight.direction));
                        float falloffFactor = saturate((angleCos - sLight.cosAngle) / (sLight.cosFalloffStart - sLight.cosAngle));
                        float3 radiance_spot = sLight.color.rgb * sLight.intensity * distanceFactor * falloffFactor;
                        Lo += CalcPBRLight(L_spot, V, N, radiance_spot, albedo, roughness, metallic, F0);
                    }

                // 4. 環境マップ (IBL)
                    float3 ambient = float3(0.0f, 0.0f, 0.0f);
                
                    if (gMaterial.enableEnvMap == 1)
                    {
                        float3 R = reflect(-V, N);
                        float3 envColor = gEnvTexture.Sample(gSampler, R).rgb;
                        envColor *= gMaterial.envIntensity;
                    
                        float3 F_ibl = FresnelSchlick(max(dot(N, V), 0.0f), F0);
                        float3 kS_ibl = F_ibl;
                        float3 kD_ibl = 1.0f - kS_ibl;
                        kD_ibl *= 1.0f - metallic;

                        float3 specular_ibl = envColor * F_ibl;
                        float3 diffuse_ibl = kD_ibl * albedo * gDirectionalLight.ambientColor;

                        ambient = diffuse_ibl + specular_ibl;
                    }
                    else
                    {
                        ambient = gDirectionalLight.ambientColor * albedo * (1.0f - metallic);
                    }

                    output.color.rgb = Lo + ambient;
                    output.color.a = gMaterial.color.a * textureColor.a;
                }
            
                if (gDirectionalLight.enableFog != 0 && output.color.a > 0.0f)
                {
                // ===========================================================
                //  距離 ＋ ハイト（高さ）フォグ
                // ===========================================================
                    float3 fogColor = gDirectionalLight.fogColor;
                    float fogStart = gDirectionalLight.fogStart;
                    float fogEnd = gDirectionalLight.fogEnd;
                    float fogHeightMin = gDirectionalLight.fogHeightMin;
                    float fogHeightMax = gDirectionalLight.fogHeightMax;

                // ① 距離による霧の濃さ (0.0 ～ 1.0)
                    float distanceToCamera = length(input.worldPosition - gCamera.worldPosition);
                    float fogRange = max(fogEnd - fogStart, 0.01f);
                    float distanceFogFactor = saturate((distanceToCamera - fogStart) / fogRange);

                // ② 高さによる霧の濃さ (0.0 ～ 1.0)
                    float heightRange = max(fogHeightMax - fogHeightMin, 0.01f);
                    float heightFogFactor = 1.0f - saturate((input.worldPosition.y - fogHeightMin) / heightRange);

                // ③ 2つのフォグを掛け合わせる
                    float finalFogFactor = distanceFogFactor * heightFogFactor;

                // ピクセルの色とフォグの色を合成
                    output.color.rgb = lerp(output.color.rgb, fogColor, finalFogFactor);
                
                // ===========================================================
                // ボリューメトリックフォグ (光の筋 / ゴッドレイ) の計算
                // ===========================================================
                    if (gDirectionalLight.volumetricIntensity > 0.0f && gDirectionalLight.volumetricSteps > 0)
                    {
                        float3 rayStart = gCamera.worldPosition;
                        float3 rayEnd = input.worldPosition;
                        float3 rayDir = rayEnd - rayStart;
                        float rayLength = length(rayDir);
                        rayDir = normalize(rayDir);
    
                    // あまり遠くまでレイを飛ばすと処理が重い＆ノイズになるので、計算距離を制限
                        float maxDistance = min(rayLength, 50.0f); // 50m先まで計算
                        float stepSize = maxDistance / (float) gDirectionalLight.volumetricSteps;
    
                        float scattering = 0.0f;
    
                    // レイマーチング (空間を少しずつ進むループ)
                        for (int i = 0; i < gDirectionalLight.volumetricSteps; ++i)
                        {
                        // 現在のチェック地点のワールド座標
                            float3 currentPos = rayStart + rayDir * (stepSize * i);
        
                        // ワールド座標から、シャドウマップ上の座標に変換
                            float4 shadowPos = mul(float4(currentPos, 1.0f), gDirectionalLight.lightViewProj);
                            shadowPos.xyz /= shadowPos.w;
        
                        // UV座標系 (0.0 ~ 1.0) に変換
                            float2 shadowUV = float2(
                            (shadowPos.x + 1.0f) / 2.0f,
                            (1.0f - shadowPos.y) / 2.0f
                        );
        
                        // 画面外チェック
                            if (shadowPos.z > 0.0f && shadowPos.z < 1.0f &&
                            shadowUV.x > 0.0f && shadowUV.x < 1.0f &&
                            shadowUV.y > 0.0f && shadowUV.y < 1.0f)
                            {
                            // シャドウマップからその地点の深度を取得
                                float depthFromLight = gShadowMap.SampleLevel(gShadowSampler, shadowUV, 0);
            
                            // 日向なら、空気中の散乱光を加算
                                if (shadowPos.z - 0.005f <= depthFromLight)
                                {
                                    scattering += 1.0f;
                                }
                            }
                        }
    
                    // 平均化して、指定した強さを掛ける
                        scattering = (scattering / (float) gDirectionalLight.volumetricSteps) * gDirectionalLight.volumetricIntensity;
    
                    // 光の筋の色を加算合成
                        float3 godRayColor = gDirectionalLight.color.rgb * gDirectionalLight.intenssity;
    
                    // ※透明なピクセルには足さない
                        if (output.color.a > 0.0f)
                        {
                            output.color.rgb += godRayColor * scattering;
                        }
                    }
                }
            
                if (gMaterial.emissive > 1.0f)
                {
                // オブジェクト本来の色（光の影響なし）
                    float3 emissiveColor = gMaterial.color.rgb * textureColor.rgb;
    
                // 発光強度分だけ、最終出力カラーに加算する
                    output.color.rgb += emissiveColor * (gMaterial.emissive - 1.0f);
                }
                break;
            }
    }
  
    return output;
}

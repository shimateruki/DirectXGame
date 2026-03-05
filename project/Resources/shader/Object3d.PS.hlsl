#include "Object3d.hlsli"

static const int kMaxPointLights = 100;
static const int kMaxSpotLights = 100;

struct PixelShanderOutput
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
    float32_t padding2;
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
    int32_t enableEnvMap;
    float envIntensity;
    float2 padding2;
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


PixelShanderOutput main(VecrtexShaderOutput input)
{
    PixelShanderOutput output;
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // アルファテスト (ガラスの場合は透明部分も描画したいので、materialTypeが0の時だけdiscardする手もあるが、一旦そのまま)
    if (gMaterial.materialType == 0 && textureColor.a <= 0.5)
    {
        discard;
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

        case 2: // Blinn-Phong (Multiple Lights)
            float32_t3 N = normalize(input.normal);
            float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

            // 1. Directional Light
            float32_t3 L = normalize(-gDirectionalLight.direction);
            NdotL = dot(N, L);
            cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
            
            float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intenssity;
            
            float32_t3 halfVector = normalize(L + toEye);
            float NdotH = dot(N, halfVector);
            float specularPow = pow(saturate(NdotH), gMaterial.shininess);
            float32_t3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intenssity * specularPow;

            // 2. Point Lights (Loop)
            float32_t3 totalPointDiffuse = float32_t3(0.0f, 0.0f, 0.0f);
            float32_t3 totalPointSpecular = float32_t3(0.0f, 0.0f, 0.0f);

            for (int i = 0; i < gPointLights.activeCount; ++i)
            {
                PointLight light = gPointLights.lights[i];
                float32_t3 directionToLight = light.position - input.worldPosition;
                float distance = length(directionToLight);
                float32_t3 L_Point = normalize(directionToLight);
                float factor = pow(saturate(-distance / light.radius + 1.0), light.decay);
                float pointCos = saturate(dot(N, L_Point));
                float32_t3 pDiffuse = gMaterial.color.rgb * textureColor.rgb * light.color.rgb * light.intensity * factor * pointCos;
                totalPointDiffuse += pDiffuse;

                if (pointCos > 0.0f)
                {
                    float32_t3 halfVectorPoint = normalize(L_Point + toEye);
                    float pointNdotH = dot(N, halfVectorPoint);
                    float pointSpecularPow = pow(saturate(pointNdotH), gMaterial.shininess);
                    float32_t3 pSpecular = light.color.rgb * light.intensity * factor * pointSpecularPow;
                    totalPointSpecular += pSpecular;
                }
            }

            // 3. Spot Lights (Loop)
            float32_t3 totalSpotDiffuse = float32_t3(0.0f, 0.0f, 0.0f);
            float32_t3 totalSpotSpecular = float32_t3(0.0f, 0.0f, 0.0f);

            for (int j = 0; j < gSpotLights.activeCount; ++j)
            {
                SpotLight light = gSpotLights.lights[j];
                float32_t3 directionToSpotLight = normalize(light.position - input.worldPosition);
                float distanceSpot = length(light.position - input.worldPosition);
                float32_t3 spotLightDir = normalize(light.direction);
                float angleCos = dot(directionToSpotLight, -spotLightDir);
                float falloffFactor = saturate((angleCos - light.cosAngle) / (light.cosFalloffStart - light.cosAngle));
                float distanceFactor = pow(saturate(-distanceSpot / light.distance + 1.0), light.decay);
                float spotCos = saturate(dot(N, directionToSpotLight));
                float32_t3 sDiffuse = gMaterial.color.rgb * textureColor.rgb * light.color.rgb * light.intensity * distanceFactor * falloffFactor * spotCos;
                totalSpotDiffuse += sDiffuse;

                if (spotCos > 0.0f)
                {
                    float32_t3 halfVectorSpot = normalize(directionToSpotLight + toEye);
                    float spotNdotH = dot(N, halfVectorSpot);
                    float spotSpecularPow = pow(saturate(spotNdotH), gMaterial.shininess);
                    float32_t3 sSpecular = light.color.rgb * light.intensity * distanceFactor * falloffFactor * spotSpecularPow;
                    totalSpotSpecular += sSpecular;
                }
            }
          // ===========================================================
            // ガラスシェーダー (Crystal Glass Shader)
            // ===========================================================
            if (gMaterial.materialType == 1)
            {
                float3 N = normalize(input.smoothNormal);
                float3 V = normalize(toEye);
                float NdotV = saturate(dot(N, V));
                float PI = 3.14159265f;

            // 環境色の設定
                float3 skyColor = float3(0.3f, 0.6f, 0.9f);
                float3 groundColor = float3(0.4f, 0.4f, 0.4f);
                float3 horizonColor = float3(1.0f, 1.0f, 1.0f);

            // ===========================================================
            // 1. プリズム屈折 (RGBを別々に曲げる！)
            // ===========================================================
            // 屈折率を色ごとに少しずらす (ガラスの分散特性)
                float iorRatio = 1.0f / 1.52f;
                float3 IOR_RGB = float3(iorRatio * 0.99f, iorRatio, iorRatio * 1.01f); // 赤、緑、青

            // 3回屈折計算を行う
                float3 RefractR = refract(-V, N, IOR_RGB.r);
                float3 RefractG = refract(-V, N, IOR_RGB.g);
                float3 RefractB = refract(-V, N, IOR_RGB.b);

            // --- 関数化できないので、3回サンプリング処理を展開します ---
            
            // [Rチャンネル]
                float2 uvR;
                uvR.x = atan2(RefractR.x, RefractR.z) / (2.0f * PI) + 0.5f;
                uvR.y = acos(clamp(RefractR.y, -1.0f, 1.0f)) / PI;
                float horizonR = pow(saturate(1.0f - abs(uvR.y - 0.5f) * 2.0f), 20.0f);
                float3 envR = (uvR.y > 0.5f) ? skyColor : groundColor;
                float colorR = lerp(envR.r, horizonColor.r, horizonR);

            // [Gチャンネル]
                float2 uvG;
                uvG.x = atan2(RefractG.x, RefractG.z) / (2.0f * PI) + 0.5f;
                uvG.y = acos(clamp(RefractG.y, -1.0f, 1.0f)) / PI;
                float horizonG = pow(saturate(1.0f - abs(uvG.y - 0.5f) * 2.0f), 20.0f);
                float3 envG = (uvG.y > 0.5f) ? skyColor : groundColor;
                float colorG = lerp(envG.g, horizonColor.g, horizonG);

            // [Bチャンネル]
                float2 uvB;
                uvB.x = atan2(RefractB.x, RefractB.z) / (2.0f * PI) + 0.5f;
                uvB.y = acos(clamp(RefractB.y, -1.0f, 1.0f)) / PI;
                float horizonB = pow(saturate(1.0f - abs(uvB.y - 0.5f) * 2.0f), 20.0f);
                float3 envB = (uvB.y > 0.5f) ? skyColor : groundColor;
                float colorB = lerp(envB.b, horizonColor.b, horizonB);

            // RGBを合成
                float3 refractionColor = float3(colorR, colorG, colorB);


            // ===========================================================
            // 2. フレネル & ダークリム
            // ===========================================================
                float F0 = 0.04f;
                float fresnel = F0 + (1.0f - F0) * pow(1.0f - NdotV, 5.0f);
                float darkRim = smoothstep(0.6f, 1.0f, 1.0f - pow(NdotV, 0.5f));

            // 表面反射 (反射は色ズレしないので1回でOK)
                float3 ReflectVec = reflect(-V, N);
                float2 uvReflect;
                uvReflect.y = acos(clamp(ReflectVec.y, -1.0f, 1.0f)) / PI;
                float3 reflectionColor = (uvReflect.y < 0.5f) ? skyColor : groundColor;

            // ===========================================================
            // 3. ダブル・スペキュラ
            // ===========================================================
                float3 L_Dir = normalize(-gDirectionalLight.direction);
                float3 H = normalize(L_Dir + V);
                float NdotH = saturate(dot(N, H));
            
                float specPowerPrimary = 8192.0f;
                float3 specPrimary = float3(1.0f, 1.0f, 1.0f) * pow(NdotH, specPowerPrimary) * 5.0f;

                float3 N_Back = normalize(N + V * 0.2f);
                float NdotH_Back = saturate(dot(N_Back, H));
                float specPowerSecondary = 512.0f;
                float3 specSecondary = float3(1.0f, 1.0f, 1.0f) * pow(NdotH_Back, specPowerSecondary) * 1.0f;

                float3 totalSpecular = specPrimary + specSecondary;

            // ===========================================================
            // 4. 集光 (Caustics)
            // ===========================================================
                float internalFocus = dot(N, -L_Dir);
                float caustic = smoothstep(0.9f, 1.0f, internalFocus);
                float3 fakeCaustics = float3(1.0f, 0.9f, 0.7f) * caustic * 2.0f;

            // ===========================================================
            // 5. 最終合成
            // ===========================================================
            // 虹色屈折 + 表面反射
                float3 bodyColor = lerp(refractionColor * (1.0f - darkRim * 0.8f), reflectionColor, fresnel);
            
                output.color.rgb = bodyColor + totalSpecular + fakeCaustics;

            // 透明度 (以前と同じ設定)
                float alphaBase = 0.02f;
                output.color.a = saturate(alphaBase + fresnel + caustic * 0.5f + (totalSpecular.r * 0.5f));
            }
            else
            {
                // メタリック（金属度）パラメータ
                float metallic = 0.8f;

                // ベースカラーは金属度が高いほど暗くなる
                float3 baseDiffuse = diffuse * (1.0f - metallic);
                
             
                float3 metalReflection = float3(0.0f, 0.0f, 0.0f); // 初期値は真っ黒

                // 定数バッファのフラグが 1(ON) の時だけ反射の計算をする
                if (gDirectionalLight.enableEnvMap == 1)
                {
                    // 1. 視線の反射ベクトルを計算
                    float3 reflectDir = reflect(-toEye, N);
                    
                    // 2. MipLevelを上げる（0.0 → 3.0など）と、画像がぼやけてマットな金属に
                    float mipLevel = 3.0f;
                    float3 envColor = gEnvTexture.SampleLevel(gSampler, reflectDir, mipLevel).rgb;

                    // 3. 定数バッファ（ImGui）からまぶしさを受け取る
                    envColor *= gDirectionalLight.envIntensity;

                    // 4. 環境の反射を金属の色とテクスチャの色と掛け合わせる
                    metalReflection = envColor * gMaterial.color.rgb * textureColor.rgb * metallic;
                }

                // 最終合成 (ONなら反射が足され、OFFなら真っ黒が足される=変化なし)
                output.color.rgb = baseDiffuse + specular + totalPointDiffuse + totalPointSpecular + totalSpotDiffuse + totalSpotSpecular;
                output.color.rgb += metalReflection;

                output.color.a = gMaterial.color.a * textureColor.a;
            
                float rimFactor = 1.0f - saturate(dot(N, toEye));
                rimFactor = pow(rimFactor, 3.0f);
                output.color.rgb += float3(1.0f, 1.0f, 1.0f) * rimFactor * 0.5f;
                output.color.rgb += gDirectionalLight.ambientColor * gMaterial.color.rgb * textureColor.rgb * (1.0f - metallic);
            }

  
            // ===========================================================
            //  距離フォグ (Distance Fog) - 全体共通
            // ===========================================================
            // ハードコードをやめて、定数バッファの値を使う
            float3 fogColor = gDirectionalLight.fogColor;
            float fogStart = gDirectionalLight.fogStart;
            float fogEnd = gDirectionalLight.fogEnd;

            float distanceToCamera = length(input.worldPosition - gCamera.worldPosition);
      
            float fogRange = max(fogEnd - fogStart, 0.01f);
            
            float fogFactor = saturate((distanceToCamera - fogStart) / fogRange);

            // フォグを適用
            output.color.rgb = lerp(output.color.rgb, fogColor, fogFactor);

            break;
    }

    return output;
}
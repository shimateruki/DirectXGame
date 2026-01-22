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
    
    // ★ここを変更: ガラスかどうかを判別するフラグを追加
    int32_t materialType; // 0:通常, 1:ガラス
    float32_t padding2; // float32_t2 から減らしてパディング調整
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intenssity;
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
                // --- 1. 共通定数とベクトル ---
                float3 N = normalize(input.smoothNormal);
                float3 V = normalize(toEye);
                float NdotV = saturate(dot(N, V));
                float PI = 3.14159265f;

                // --- 2. フレネル反射 (輪郭の透明度変化) ---
                float F0 = 0.02f;
                float fresnel = F0 + (1.0f - F0) * pow(1.0f - NdotV, 5.0f);

                // --- 3. スペキュラ (太陽光の鋭い反射) ---
                float3 L_Dir = normalize(-gDirectionalLight.direction);
                float3 H = normalize(L_Dir + V);
                float NdotH = saturate(dot(N, H));
                float specPower = 2048.0f;
                float3 specular = float3(1.0f, 1.0f, 1.0f) * pow(NdotH, specPower) * 2.0f;

                // --- 4. 集光現象 (影側の発光) ---
                float internalFocus = dot(N, -L_Dir);
                float caustic = smoothstep(0.8f, 1.0f, internalFocus) * 0.8f;
                float3 fakeCaustics = float3(1.0f, 0.9f, 0.7f) * caustic;

                // --- 5. 環境反射 (球面マッピング + 二重反射) ---
                float3 R = reflect(-V, N);
                
                // 表面UV
                float2 uvFront;
                uvFront.x = atan2(R.x, R.z) / (2.0f * PI) + 0.5f;
                uvFront.y = acos(clamp(R.y, -1.0f, 1.0f)) / PI;

                // 裏面UV (厚みを持たせるためのズレ)
                float2 uvBack = uvFront + float2(0.06f, 0.06f);

                // 背景グラデーション
                float3 topColor = float3(1.0f, 1.0f, 1.0f);
                float3 bottomColor = float3(0.05f, 0.05f, 0.1f);
                float3 envColor = lerp(topColor, bottomColor, uvFront.y);

                // スタジオ照明の計算 (表面)
                float w1_f = smoothstep(0.02f, 0.05f, abs(uvFront.x - 0.2f));
                float h1_f = smoothstep(0.05f, 0.1f, abs(uvFront.y - 0.4f));
                float box1_f = (1.0f - w1_f) * (1.0f - h1_f);
                float w2_f = smoothstep(0.01f, 0.03f, abs(uvFront.x - 0.7f));
                float h2_f = smoothstep(0.1f, 0.15f, abs(uvFront.y - 0.6f));
                float box2_f = (1.0f - w2_f) * (1.0f - h2_f);
                float lightsFront = box1_f + box2_f;
                
                // スタジオ照明の計算 (裏面)
                float w1_b = smoothstep(0.02f, 0.05f, abs(uvBack.x - 0.2f));
                float h1_b = smoothstep(0.05f, 0.1f, abs(uvBack.y - 0.4f));
                float box1_b = (1.0f - w1_b) * (1.0f - h1_b);
                float w2_b = smoothstep(0.01f, 0.03f, abs(uvBack.x - 0.7f));
                float h2_b = smoothstep(0.1f, 0.15f, abs(uvBack.y - 0.6f));
                float box2_b = (1.0f - w2_b) * (1.0f - h2_b);
                float lightsBack = (box1_b + box2_b) * 0.8f;

                float lightsTotal = lightsFront + lightsBack;
                float3 fakeLights = float3(1.0f, 1.0f, 1.0f) * lightsTotal;

                // --- 6. 虹色リムライト (プリズム効果) ---
                float rim = pow(1.0f - NdotV, 3.0f);
                float3 rainbowRim;
                rainbowRim.r = rim * 0.8f;
                rainbowRim.g = rim * 0.6f;
                rainbowRim.b = rim * 1.5f;

                // --- 7. 最終合成 ---
                float3 transmissionColor = float3(0.0f, 0.0f, 0.0f);
                
                output.color.rgb = transmissionColor
                                 + (envColor * fresnel)
                                 + fakeLights
                                 + specular
                                 + rainbowRim
                                 + fakeCaustics;

                // --- 8. アルファブレンド ---
                float lightAlpha = saturate(lightsTotal * 0.7f);
                output.color.a = saturate(0.05f + fresnel + lightAlpha + caustic * 0.5f);
            }
            else
            {
            
                output.color.rgb = diffuse + specular + totalPointDiffuse + totalPointSpecular + totalSpotDiffuse + totalSpotSpecular;
                output.color.a = gMaterial.color.a * textureColor.a;

                float rimFactor = 1.0f - saturate(dot(N, toEye));
                rimFactor = pow(rimFactor, 3.0f);
                output.color.rgb += float3(1.0f, 1.0f, 1.0f) * rimFactor * 0.5f;
                output.color.rgb += float3(0.02f, 0.02f, 0.02f) * gMaterial.color.rgb * textureColor.rgb;
            }

            // ===========================================================
            // ★ 距離フォグ (Distance Fog) - 全体共通
            // ===========================================================
            float3 fogColor = float3(0.1f, 0.1f, 0.1f);
            float distanceToCamera = length(input.worldPosition - gCamera.worldPosition);
            float fogStart = 1000.0f;
            float fogEnd = 5000.0f;
            float fogFactor = saturate((distanceToCamera - fogStart) / (fogEnd - fogStart));

            // フォグを適用
            output.color.rgb = lerp(output.color.rgb, fogColor, fogFactor);

            break;
    }

    return output;
}
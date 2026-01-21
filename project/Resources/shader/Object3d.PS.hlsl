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

            if (gMaterial.materialType == 1)
            {
                // --------------------------------------------------------
                // 1. スムース・ハイライト（三角形の角を落とす）
                // --------------------------------------------------------
                // 鋭いハイライト(256)と、少し広いハイライト(32)を混ぜることで、
                // ポリゴンの継ぎ目を「中間の光」で埋めて隠します。
                float3 L = normalize(-gDirectionalLight.direction);
                float3 halfVector = normalize(L + toEye);
                float dotH = saturate(dot(N, halfVector));
                
                float specSharp = pow(dotH, 256.0f); // 鋭い点
                float specSoft = pow(dotH, 32.0f); // 柔らかい光（これがポリゴンの角を隠す）
                
                float3 directionalSpecular = gDirectionalLight.color.rgb * gDirectionalLight.intenssity * (specSharp + specSoft * 0.5f);

                // ポイント/スポットライトのスペキュラも同様に「鋭さ」を調整
                // ※ totalSpotSpecular 等をそのまま使うと角が出るので、少し減衰をかける
                float3 finalSpecular = (directionalSpecular + totalPointSpecular + totalSpotSpecular);

                // --------------------------------------------------------
                // 2. フレネル反射（縁の輝き）
                // --------------------------------------------------------
                float fresnel = 1.0f - saturate(dot(N, toEye));
                float fresnelEdge = pow(fresnel, 4.0f); // 縁の鋭い光
                float fresnelBody = pow(fresnel, 2.0f); // 全体的な薄い反射

                // --------------------------------------------------------
                // 3. スポットライト・ライトカラーの透過
                // --------------------------------------------------------
                // ガラスなので Diffuse ではなく「内側を通り抜ける光」として計算
                float3 lightInBody = (totalPointDiffuse + totalSpotDiffuse) * 0.5f;
                float3 glassColor = gMaterial.color.rgb * textureColor.rgb;

                // --------------------------------------------------------
                // 4. 最終色の合成
                // --------------------------------------------------------
                float3 rimColor = float3(1.0f, 1.0f, 1.0f);
                
                // (環境光) + (ライトによる内側の発光) + (縁の反射) + (ハイライト)
                output.color.rgb = (glassColor * 0.1f) + (lightInBody * glassColor) + (rimColor * fresnelEdge * 1.5f) + finalSpecular;

                // --------------------------------------------------------
                // 5. 透明度の計算（ここが最重要！）
                // --------------------------------------------------------
                // ハイライト(specular)をアルファに強く反映させすぎると三角形が見えるので、
                // ハイライトの寄与を下げ、フレネルをメインにします。
                float specularAlpha = saturate(length(finalSpecular) * 0.5f);
                float baseAlpha = 0.1f; // 真ん中の透明度（もっと透かしたいなら 0.05）
                
                output.color.a = saturate(baseAlpha + fresnelBody * 0.8f + specularAlpha);
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
            float fogStart = 10.0f;
            float fogEnd = 50.0f;
            float fogFactor = saturate((distanceToCamera - fogStart) / (fogEnd - fogStart));

            // フォグを適用
            output.color.rgb = lerp(output.color.rgb, fogColor, fogFactor);

            break;
    }

    return output;
}
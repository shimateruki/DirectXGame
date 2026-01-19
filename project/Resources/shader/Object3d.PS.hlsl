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
    float32_t2 padding2;
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

    if (textureColor.a <= 0.5)
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

            // Final Composition
            output.color.rgb = diffuse + specular + totalPointDiffuse + totalPointSpecular + totalSpotDiffuse + totalSpotSpecular;
            output.color.a = gMaterial.color.a * textureColor.a;
        // 1. 視線ベクトルと法線の角度を見る
    // N (法線) と toEye (カメラへの方向) の内積をとる
    // 正面ほど 1.0、輪郭(90度)ほど 0.0 になる
    float rimFactor = 1.0f - saturate(dot(N, toEye));

    // 2. 範囲を調整する (powで絞る)
    // 3.0f という数字を大きくすると、もっと細い線になります
    rimFactor = pow(rimFactor, 3.0f);

    // 3. 光の色を決める (とりあえず白)
    float3 rimColor = float3(1.0f, 1.0f, 1.0f);

    // 4. 強さを決める (0.5くらいが丁度いいかも)
    float rimIntensity = 0.5f;

    // 最終カラーに足し算する！
    output.color.rgb += rimColor * rimFactor * rimIntensity;
        // ホラーゲームならここを 0.02f (2%) くらいにする
    // 普通のゲームなら 0.1f (10%) ～ 0.2f (20%) くらい
            float3 ambientColor = float3(0.02f, 0.02f, 0.02f);

    // 元の色(テクスチャなど)に対して、最低限の明るさを保証する
    output.color.rgb += ambientColor * gMaterial.color.rgb * textureColor.rgb;
        
        // -----------------------------------------------------------
    // ★距離フォグ (Distance Fog) の追加
    // -----------------------------------------------------------

    // 1. 背景色（フォグの色）
    // ※ 本来は C++ から送るべきですが、今は背景クリア色(画面の背景色)と同じにします
    float3 fogColor = float3(0.1f, 0.1f, 0.1f); 

    // 2. カメラとピクセルの距離を測る
    // input.worldPosition : ピクセルの場所
    // gCamera.worldPosition : カメラの場所 (RimLightで使ったはず！)
    float distance = length(input.worldPosition - gCamera.worldPosition);

    // 3. フォグのかかり具合を計算
    // 10.0f から霧がかかり始め、50.0f で真っ白(完全に霧)になる設定
    float fogStart = 10.0f;
    float fogEnd = 50.0f;

    // 線形補間 (0.0=霧なし ～ 1.0=完全に霧)
    float fogFactor = saturate((distance - fogStart) / (fogEnd - fogStart));

    // 4. 元の色とフォグの色を混ぜる
    // fogFactor が増えるほど fogColor に近づく
    output.color.rgb = lerp(output.color.rgb, fogColor, fogFactor);
            break;
    }

    return output;
}
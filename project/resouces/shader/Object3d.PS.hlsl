#include "Object3d.hlsli"



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

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
struct pixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};
struct Camera
{
    float32_t3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2);
Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

PixelShanderOutput main(VecrtexShaderOutput input)
{
    PixelShanderOutput output;
    //float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    float NdotL;
    float cos;
    float32_t3 specular = float32_t3(0.0f, 0.0f, 0.0f);
    switch (gMaterial.selectedLighting)
    {
        case 0:

            output.color = gMaterial.color * textureColor;
            break;
        case 1: 
            cos = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));
            // RGBの計算
            output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intenssity;
            // アルファの計算を分離
            output.color.a = gMaterial.color.a * textureColor.a;
            break;
        case 2:
            float32_t3 N = normalize(input.normal);
            float32_t3 L = normalize(-gDirectionalLight.direction); // ライトへの方向
            NdotL = dot(N, L);
            cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
            float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intenssity;
            float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
            float32_t3 halfVector = normalize(L + toEye);
            float NdotH = dot(N, halfVector);
            float specularPow = pow(saturate(NdotH), gMaterial.shininess);
             
             // 鏡面反射色
            specular = gDirectionalLight.color.rgb * gDirectionalLight.intenssity * specularPow;
             
             // 合成
            output.color.rgb = diffuse + specular;
            output.color.a = gMaterial.color.a * textureColor.a;
            break;
    }
    if (textureColor.a <= 0.5)
    {
        discard;
    }

        return output;
}
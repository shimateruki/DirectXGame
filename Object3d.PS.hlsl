#include "Object3d.hlsli"



struct PixelShanderOutput
{
    float32_t4 color : SV_TARGET0;
    
};
struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
     int32_t selectedLighting;
    
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
            break;
        case 2:
             NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
             cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
            output.color.rgb = gMaterial.color.rgb * textureColor.rgb * gDirectionalLight.color.rgb * cos * gDirectionalLight.intenssity;
            output.color.a = gMaterial.color.a * textureColor.a;
        break;
    }

   
      
    return output;
}
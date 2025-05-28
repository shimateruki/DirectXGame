#include "Object3d.hlsli"



struct PixelShanderOutput
{
    float32_t4 color : SV_TARGET0;
    
};
struct Material
{
    float32_t4 color;
};

ConstantBuffer<Material> gMaterial: register(b0);

struct pixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

PixelShanderOutput main(VecrtexShaderOutput input)
{
    PixelShanderOutput output;
    float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    output.color = gMaterial.color * textureColor;
      
    return output;
}
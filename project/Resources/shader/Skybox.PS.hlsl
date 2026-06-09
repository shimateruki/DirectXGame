// Skybox.PS.hlsl
#include "Skybox.hlsli"

// Cubemap texture.
TextureCube<float4> skyboxTexture : register(t0);
SamplerState smp : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    // Sample the cubemap with a 3D direction vector.
    float4 textureColor = skyboxTexture.Sample(smp, input.texcoord);
    
    return textureColor;
}

// Skybox.VS.hlsl
#include "Skybox.hlsli"

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    // Keep only camera rotation so the skybox never follows camera translation.
    float3x3 view3x3 = (float3x3) view;
    
    // Transform the direction by the view rotation.
    float3 viewPos = mul(input.position.xyz, view3x3);
    
    // Project to clip space.
    output.position = mul(float4(viewPos, 1.0f), projection);
    
    // Force the skybox to the far plane after perspective divide.
    output.position.z = output.position.w;
    
    // Use local cube coordinates as the cubemap sampling direction.
    output.texcoord = input.position.xyz;
    
    return output;
}

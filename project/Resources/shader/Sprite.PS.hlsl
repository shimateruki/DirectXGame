

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
cbuffer SpriteMaterial : register(b1)
{
    float4 color;
    int enableLighting;
    float3 padding1;
    float4x4 uvTransform;
    float emissive;
    float3 padding2;
};
float4 main(VertexOutput input) : SV_TARGET
{
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    // ベースとなる色
    float4 finalColor = textureColor * color;

 
    if (emissive > 1.0f)
    {
        finalColor.rgb *= emissive;
    }

    return finalColor;
}
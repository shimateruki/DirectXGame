// Skybox.hlsli

struct VertexShaderInput
{
    float4 position : POSITION0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0;
};

cbuffer ViewProjection : register(b0)
{
    
    row_major matrix view;
    row_major matrix projection;
};
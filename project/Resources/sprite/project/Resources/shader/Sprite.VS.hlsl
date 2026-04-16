

struct VertexInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

cbuffer TransformationMatrix : register(b0)
{
    float4x4 WVP;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(input.position, WVP);
    output.texcoord = input.texcoord;
    return output;
}
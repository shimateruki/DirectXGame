

// DrawShadowが送ってくる標準バッファ構造に合わせる
struct TransformationMatrix
{
    matrix WVP;
    matrix world;
    matrix WorldInverseTranspose;
};

cbuffer WVPBuffer : register(b0)
{
    TransformationMatrix WVPData;
};

struct VertexInput
{
    float4 pos : POSITION; // float3 -> float4 に変更！
    float2 uv : TEXCOORD; // NORMALより先に来るように順番を変更！
    float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0; // 明示的に0にする
    float3 normal : NORMAL;
    float4 clipPos : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    
    // 行列計算
    output.svpos = mul(float4(input.pos.xyz, 1.0f), WVPData.WVP);
    output.normal = normalize(mul(input.normal, (float3x3) WVPData.WorldInverseTranspose));
    output.uv = input.uv;
    output.clipPos = output.svpos;
    
    return output;
}
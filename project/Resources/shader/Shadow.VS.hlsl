// ===================================================
// Shadow.VS.hlsl (影生成専用の頂点シェーダー)
// ===================================================

// 定数バッファ (b0)
cbuffer gTransformationMatrix : register(b0)
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

// アニメーション用ボーン行列 (t1)
StructuredBuffer<float4x4> gMatrixPalette : register(t1);

// 平行光源データ (b1)
cbuffer gDirectionalLight : register(b1)
{
    float4 color;
    float3 direction;
    float intenssity;
    float3 ambientColor;
    float fogStart;
    float fogEnd;
    float3 fogColor;
    float4x4 viewProjection; // 太陽の目線の行列
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weight : WEIGHT0;
    float4 index : INDEX0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    // ボーンインデックス
    int4 iIndex = int4(input.index);

    // スキニング計算 (Linear Blend Skinning)
    float4x4 skinningMatrix =
        gMatrixPalette[iIndex.x] * input.weight.x +
        gMatrixPalette[iIndex.y] * input.weight.y +
        gMatrixPalette[iIndex.z] * input.weight.z +
        gMatrixPalette[iIndex.w] * input.weight.w;

    // 1. モデルの座標をスキニングを考慮して変換
    float4 skinnedPosition = mul(input.position, skinningMatrix);
    
    // 2. ワールド座標を計算
    float4 worldPosition = mul(skinnedPosition, World);
    
    // ★超重要: カメラの行列ではなく、太陽の行列(viewProjection)を使って影の形を描く
    output.position = mul(worldPosition, viewProjection);

    return output;
}
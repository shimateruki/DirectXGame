#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);
ConstantBuffer<TransformationMatrix> gShadowMatrix : register(b1);
// スキニング用行列パレット
StructuredBuffer<float32_t4x4> gMatrixPalette : register(t1);

struct VertexShanderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t4 weight : WEIGHT0;
    float32_t4 index : INDEX0;
    float32_t3 tangent : TANGENT0;
};

VecrtexShaderOutput main(VertexShanderInput input)
{
    VecrtexShaderOutput output;

    // ボーンインデックスを整数にキャスト
    int4 iIndex = int4(input.index);

    // -------------------------------------------------------------------------
    // スキニング行列の計算 (Linear Blend Skinning)
    // -------------------------------------------------------------------------
    float32_t4x4 skinningMatrix =
        gMatrixPalette[iIndex.x] * input.weight.x +
        gMatrixPalette[iIndex.y] * input.weight.y +
        gMatrixPalette[iIndex.z] * input.weight.z +
        gMatrixPalette[iIndex.w] * input.weight.w;

    // -------------------------------------------------------------------------
    // スキニングの適用
    // -------------------------------------------------------------------------
    // 頂点座標の変換
    float32_t4 skinnedPosition = mul(input.position, skinningMatrix);

    // 法線ベクトルの変換 (回転のみ適用)
    float32_t3 skinnedNormal = mul(input.normal, (float32_t3x3) skinningMatrix);
    float32_t3 skinnedTangent = mul(input.tangent, (float32_t3x3) skinningMatrix);
    // スムース法線の計算 (球体近似 / ローカル座標を法線として扱う)
    float32_t3 localSmoothNormal = normalize(input.position.xyz);
    float32_t3 skinnedSmoothNormal = mul(localSmoothNormal, (float32_t3x3) skinningMatrix);

    // -------------------------------------------------------------------------
    // 座標変換と出力
    // -------------------------------------------------------------------------
    // WVP変換 (Local -> World -> View -> Proj)
    output.position = mul(skinnedPosition, gTransformationMatrix.WVP);

    output.texcoord = input.texcoord;

    // 法線のワールド変換
    output.normal = normalize(mul(skinnedNormal, (float32_t3x3) gTransformationMatrix.WorldInverseTranspose));
    output.tangent = normalize(mul(skinnedTangent, (float32_t3x3) gTransformationMatrix.World));
    // スムース法線のワールド変換 (ガラス描画等で使用)
    output.smoothNormal = normalize(mul(skinnedSmoothNormal, (float32_t3x3) gTransformationMatrix.World));

    // ワールド座標 (PixelShader用)
    output.worldPosition = mul(skinnedPosition, gTransformationMatrix.World).xyz;
    output.shadowPosition = mul(skinnedPosition, gShadowMatrix.WVP);
    return output;
}
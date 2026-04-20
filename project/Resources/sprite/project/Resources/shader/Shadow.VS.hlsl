struct VSInput
{
    float4 position : POSITION;
    float2 texcoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float4 boneWeights : WEIGHT;
    float4 boneIndices : INDEX;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

// =========================================================
// ★注意：registerの番号(b1, t0など)は、
// メインの Object3d.VS.hlsl と全く同じ番号に合わせてください！
// =========================================================
struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 world;
    float4x4 WorldInverseTranspose;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);
struct Bone
{
    float4x4 finalMatrix;
};
StructuredBuffer<Bone> gBones : register(t0); // ← ボーンデータの番号

VSOutput main(VSInput input)
{
    VSOutput output;

    // 1. ボーンアニメーションの計算（キャラクターを走らせる）
    float4x4 boneMatrix =
        gBones[input.boneIndices.x].finalMatrix * input.boneWeights.x +
        gBones[input.boneIndices.y].finalMatrix * input.boneWeights.y +
        gBones[input.boneIndices.z].finalMatrix * input.boneWeights.z +
        gBones[input.boneIndices.w].finalMatrix * input.boneWeights.w;

    float4 localPos = mul(input.position, boneMatrix);

    // 2. 太陽目線のWVP行列で座標変換して「影の形」を作る
    output.position = mul(localPos, gTransformationMatrix.WVP);
    
    return output;
}
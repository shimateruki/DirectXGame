#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);
ConstantBuffer<TransformationMatrix> gShadowMatrix : register(b1);
StructuredBuffer<float32_t4x4> gMatrixPalette : register(t1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t4 weight : WEIGHT0;
    float32_t4 index : INDEX0;
    float32_t3 tangent : TANGENT0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    float32_t4 skinnedPosition = input.position;
    float32_t3 skinnedNormal = input.normal;
    float32_t3 skinnedTangent = input.tangent;
    float32_t3 localSmoothNormal = normalize(input.position.xyz);
    float32_t3 skinnedSmoothNormal = localSmoothNormal;

    // Zero weights identify vertices already transformed by the compute skinning pass.
    const float32_t weightSum = dot(
        abs(input.weight),
        float32_t4(1.0f, 1.0f, 1.0f, 1.0f));
    if (weightSum > 1.0e-6f)
    {
        const int4 indices = int4(input.index);
        const float32_t4 normalizedWeights = input.weight / weightSum;
        const float32_t4x4 skinningMatrix =
            gMatrixPalette[indices.x] * normalizedWeights.x +
            gMatrixPalette[indices.y] * normalizedWeights.y +
            gMatrixPalette[indices.z] * normalizedWeights.z +
            gMatrixPalette[indices.w] * normalizedWeights.w;

        skinnedPosition = mul(input.position, skinningMatrix);
        skinnedNormal = mul(input.normal, (float32_t3x3)skinningMatrix);
        skinnedTangent = mul(input.tangent, (float32_t3x3)skinningMatrix);
        skinnedSmoothNormal = mul(localSmoothNormal, (float32_t3x3)skinningMatrix);
    }

    output.position = mul(skinnedPosition, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(
        skinnedNormal,
        (float32_t3x3)gTransformationMatrix.WorldInverseTranspose));
    output.tangent = normalize(mul(
        skinnedTangent,
        (float32_t3x3)gTransformationMatrix.World));
    output.smoothNormal = normalize(mul(
        skinnedSmoothNormal,
        (float32_t3x3)gTransformationMatrix.World));
    output.worldPosition = mul(skinnedPosition, gTransformationMatrix.World).xyz;
    output.shadowPosition = mul(skinnedPosition, gShadowMatrix.WVP);

    return output;
}

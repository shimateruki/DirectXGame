#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);
StructuredBuffer<float32_t4x4> gMatrixPalette : register(t1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t3 tangent : TANGENT0;
    float32_t4 weight : WEIGHT0;
    float32_t4 index : INDEX0;
};

VecrtexShaderOutput main(VertexShaderInput input)
{
    VecrtexShaderOutput output;

    int4 iIndex = int4(input.index);
    float32_t4x4 skinningMatrix =
        gMatrixPalette[iIndex.x] * input.weight.x +
        gMatrixPalette[iIndex.y] * input.weight.y +
        gMatrixPalette[iIndex.z] * input.weight.z +
        gMatrixPalette[iIndex.w] * input.weight.w;

    float32_t4 skinnedPosition = mul(input.position, skinningMatrix);

    output.position = mul(skinnedPosition, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = input.normal;
    output.worldPosition = mul(skinnedPosition, gTransformationMatrix.World).xyz;
    output.smoothNormal = input.normal;
    output.tangent = input.tangent;
    output.shadowPosition = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    output.localPosition = input.position.xyz;
    return output;
}

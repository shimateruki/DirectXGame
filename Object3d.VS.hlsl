#include "Object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);




struct VertexShanderInput
{
    float32_t4 position :POSITION0;
    float32_t2 texcoord : TEXCOORD0;
};

VecrtexShaderOutput main(VertexShanderInput input)
{
    VecrtexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    return output;
}


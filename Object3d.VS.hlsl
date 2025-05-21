struct TransformationMatrix
{
    float32_t4x4 WVP;
};
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);


struct VertexShanderOutput
{
    float32_t4 position : SV_POSITION;
};

struct VertexShanderInput
{
    float32_t4 position :POSITION0;
};

VertexShanderOutput main(VertexShanderInput input)
{
    VertexShanderOutput output;
    output.position = mul(input.position, gTransformationMatrix.WVP);
    return output;
}


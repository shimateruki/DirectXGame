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

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 world;
    float4x4 WorldInverseTranspose;
};

struct Bone
{
    float4x4 finalMatrix;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);
StructuredBuffer<Bone> gBones : register(t0);

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 localPosition = input.position;

    // Zero weights identify vertices already transformed by the compute skinning pass.
    const float weightSum = dot(
        abs(input.boneWeights),
        float4(1.0f, 1.0f, 1.0f, 1.0f));
    if (weightSum > 1.0e-6f)
    {
        const uint4 indices = uint4(input.boneIndices);
        const float4 normalizedWeights = input.boneWeights / weightSum;
        const float4x4 boneMatrix =
            gBones[indices.x].finalMatrix * normalizedWeights.x +
            gBones[indices.y].finalMatrix * normalizedWeights.y +
            gBones[indices.z].finalMatrix * normalizedWeights.z +
            gBones[indices.w].finalMatrix * normalizedWeights.w;
        localPosition = mul(input.position, boneMatrix);
    }

    output.position = mul(localPosition, gTransformationMatrix.WVP);
    return output;
}

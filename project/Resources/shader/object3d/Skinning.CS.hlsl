struct VertexData
{
    float4 position;
    float2 texcoord;
    float3 normal;
    float3 tangent;
    float4 boneWeights;
    float4 boneIndices;
};

StructuredBuffer<VertexData> gInputVertices : register(t0);
StructuredBuffer<float4x4> gMatrixPalette : register(t1);
RWStructuredBuffer<VertexData> gOutputVertices : register(u0);

cbuffer SkinningDispatchConstants : register(b0)
{
    uint gVertexCount;
    uint gBoneCount;
};

float3 NormalizeOrFallback(float3 value, float3 fallbackValue)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8f ? value * rsqrt(lengthSquared) : fallbackValue;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gVertexCount)
    {
        return;
    }

    VertexData inputVertex = gInputVertices[dispatchThreadId.x];
    VertexData outputVertex = inputVertex;

    const float weightSum = dot(
        abs(inputVertex.boneWeights),
        float4(1.0f, 1.0f, 1.0f, 1.0f));
    if (gBoneCount > 0 && weightSum > 1.0e-6f)
    {
        const float4 weights = inputVertex.boneWeights / weightSum;
        const uint4 maximumIndex = uint4(
            gBoneCount - 1,
            gBoneCount - 1,
            gBoneCount - 1,
            gBoneCount - 1);
        const uint4 indices = min(uint4(inputVertex.boneIndices), maximumIndex);
        const float4x4 skinningMatrix =
            gMatrixPalette[indices.x] * weights.x +
            gMatrixPalette[indices.y] * weights.y +
            gMatrixPalette[indices.z] * weights.z +
            gMatrixPalette[indices.w] * weights.w;

        outputVertex.position = mul(inputVertex.position, skinningMatrix);
        outputVertex.normal = NormalizeOrFallback(
            mul(inputVertex.normal, (float3x3)skinningMatrix),
            inputVertex.normal);
        outputVertex.tangent = NormalizeOrFallback(
            mul(inputVertex.tangent, (float3x3)skinningMatrix),
            inputVertex.tangent);
    }

    // Zero weights mark vertices that have already been skinned for the graphics pass.
    outputVertex.boneWeights = float4(0.0f, 0.0f, 0.0f, 0.0f);
    outputVertex.boneIndices = float4(0.0f, 0.0f, 0.0f, 0.0f);
    gOutputVertices[dispatchThreadId.x] = outputVertex;
}

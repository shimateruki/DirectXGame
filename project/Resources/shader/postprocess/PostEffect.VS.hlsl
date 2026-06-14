struct VSOutput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
};

static const float4 kPositions[3] = {
    float4(-1.0f, 1.0f, 0.0f, 1.0f),  // 左上
    float4(3.0f, 1.0f, 0.0f, 1.0f),   // 右上
    float4(-1.0f, -3.0f, 0.0f, 1.0f), // 左下
};

static const float2 kTexcoords[3] = {
    float2(0.0f, 0.0f), // 左上
    float2(2.0f, 0.0f), // 右上
    float2(0.0f, 2.0f), // 左下
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.svpos = kPositions[vertexID];
    output.uv = kTexcoords[vertexID];
    return output;
}
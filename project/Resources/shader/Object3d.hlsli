struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t3 worldPosition : POSITION0;
    float32_t3 smoothNormal : NORMAL1;
    float32_t3 tangent : TANGENT0;
    float32_t4 shadowPosition : TEXCOORD1;
};
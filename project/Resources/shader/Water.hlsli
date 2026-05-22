cbuffer cbWVP : register(b0)
{
    matrix WVP;
    matrix world;
    matrix WorldInverseTranspose;
};

cbuffer cbWaterParam : register(b1)
{
    float time;
    float waveSpeed;
    float waveHeight;
    float waveFrequency;
    float flowSpeedX;
    float flowSpeedY;
    float uvOffsetX;
    float uvOffsetY;
};

cbuffer cbMaterial : register(b2)
{
    float4 color;
    int enableLighting;
    float3 padding1;
    matrix uvTransform;
    int selectedLighting;
    float shininess;
    int materialType;
    float roughness;
    float metallic;
    
    int enableNormalMap;
    int enableEnvMap;
    float envIntensity;
    float emissive;
    float padding2;
};

struct VSInput
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float4 boneWeights : WEIGHT;
    float4 boneIndices : INDEX;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 screenPos : TEXCOORD1;
};

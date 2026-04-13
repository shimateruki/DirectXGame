// [0] WVP (b0レジスタ) : C++側の wvpResource_ と完全一致
cbuffer cbWVP : register(b0)
{
    matrix WVP;
    matrix world;
    matrix WorldInverseTranspose;
};

// [1] 波パラメータ (b1レジスタ) : C++側の waterParamResource_ と完全一致
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

// [2] マテリアル (b2レジスタ) : Inspectorの色情報を受け取る
cbuffer cbMaterial : register(b2)
{
    float4 color;
};

struct VSInput
{
    float4 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 screenPos : TEXCOORD1;
};
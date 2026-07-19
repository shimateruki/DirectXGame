// [0] WVP constants. Must match the C++ WVP buffer.
cbuffer cbWVP : register(b0)
{
    matrix WVP;
    matrix world;
    matrix WorldInverseTranspose;
};

// [1] Shared effect parameters. Must match MeshRenderer::WaterParamForGPU.
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
    float effectType;
    float effectScale;
    float effectSoftness;
    float effectIntensity;
    float3 cameraWorldPosition;
    float billboardScale;
    float effectScaleX;
    float effectScaleY;
    float effectScaleZ;
    float waterParamPadding0;
    float3 waterLightDirection;
    float waterLightIntensity;
    float3 waterLightColor;
    float waterParamPadding1;
};

// [2] Material color from the inspector.
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
    float3 localPos : TEXCOORD2;
    float4 screenPos : TEXCOORD1;
};

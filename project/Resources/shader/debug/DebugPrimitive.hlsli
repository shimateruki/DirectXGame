struct VSInput
{
    float4 position : POSITION0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

// Per-object transform matrix.
cbuffer ObjectTransform : register(b0)
{
    matrix worldViewProjection; // World x View x Projection
};

// Shared draw color.
cbuffer ObjectColor : register(b1)
{
    float4 color;
};

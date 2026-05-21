#include "Object3d.hlsli"

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

struct OutlineParams
{
    float32_t3 localMin;
    float32_t thickness;
    float32_t3 localMax;
    float32_t padding;
};

ConstantBuffer<OutlineParams> gOutline : register(b5);

PixelShaderOutput main(VecrtexShaderOutput input)
{
    PixelShaderOutput output;
    float3 size = max(gOutline.localMax - gOutline.localMin, float3(0.0001f, 0.0001f, 0.0001f));
    float3 nearMin = abs(input.localPosition - gOutline.localMin) / size;
    float3 nearMax = abs(input.localPosition - gOutline.localMax) / size;
    float3 nearEdge = min(nearMin, nearMax);

    float edgeCount =
        (nearEdge.x < gOutline.thickness ? 1.0f : 0.0f) +
        (nearEdge.y < gOutline.thickness ? 1.0f : 0.0f) +
        (nearEdge.z < gOutline.thickness ? 1.0f : 0.0f);

    if (edgeCount < 2.0f)
    {
        discard;
    }

    output.color = float32_t4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}

#include "DebugPrimitive.hlsli"

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(input.position, worldViewProjection);
    return output;
}


#include "DebugPrimitive.hlsli" 

VSOutput main(VSInput input) {
    VSOutput output;
    // ワールド座標系での位置に変換し、さらにVP変換を行う
    output.position = mul(input.position, worldViewProjection);
    return output;
}
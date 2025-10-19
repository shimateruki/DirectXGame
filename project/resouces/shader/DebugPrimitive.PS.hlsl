
#include "DebugPrimitive.hlsli" // 共通ヘッダーをインクルード

float4 main(VSOutput input) : SV_TARGET
{
    // 定数バッファで指定された色をそのまま出力
    return color;
}
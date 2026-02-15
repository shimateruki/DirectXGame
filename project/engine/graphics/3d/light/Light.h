#pragma once
#include "engine/utility/math/Math.h"
#include <cstdint> // int32_t

/// <summary>
/// 平行光源
/// </summary>
struct DirectionalLight {
    Vector4 color;         // 16 byte
    Vector3 direction;     // 12 byte
    float intensity;       // 4 byte

    Vector3 ambientColor;  // 12 byte
    float fogStart;        // 4 byte (パディングの代わりに入れる)

    float fogEnd;          // 4 byte
    Vector3 fogColor;      // 12 byte
};

/// <summary>
/// 点光源
/// </summary>
struct PointLight {
    Vector4 color;      // ライトの色
    Vector3 position;   // 位置
    float intensity;    // 輝度
    float radius;       // 影響範囲
    float decay;        // 減衰率
    int32_t isActive;   // 有効フラグ
    float padding;      // アライメント用
};

// シェーダーに渡すライトの定数バッファ (CBV)
// (ポイントライトの最大数を定義)
const int kMaxPointLights = 50;

struct LightGroup {
    DirectionalLight directionalLight;
    PointLight pointLights[kMaxPointLights];
};
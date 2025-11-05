#pragma once
#include "engine/base/Math.h"
#include <cstdint> // int32_t

/// <summary>
/// 平行光源
/// </summary>
struct DirectionalLight {
    Vector4 color;      // ライトの色
    Vector3 direction;  // ライトの向き
    float intensity;    // 輝度
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
    int32_t isActive;   // 有効フラグ (シェーダーでのbool代わり)
    float padding;      // アライメント用
};

// シェーダーに渡すライトの定数バッファ (CBV)
// (ポイントライトの最大数を定義)
const int kMaxPointLights = 4;

struct LightGroup {
    DirectionalLight directionalLight;
    PointLight pointLights[kMaxPointLights];
};
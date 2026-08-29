#pragma once

#include "Math.h"
#include "WinApp.h"
#include "json.hpp"

namespace SpriteLayoutScaler {
/// JSONへ保存した設計解像度から、現在のクライアント領域へ変換する倍率です。
struct LayoutScale {
    Vector2 designResolution = { 1920.0f, 1080.0f };
    Vector2 ratio = { 1.0f, 1.0f };
    bool enabled = false;
};

inline Vector2 ReadVector2(const nlohmann::json& value, const Vector2& fallback) {
    if (!value.is_array() || value.size() < 2) return fallback;
    return { value[0].get<float>(), value[1].get<float>() };
}

inline float AtLeastOne(float value) { return value < 1.0f ? 1.0f : value; }

inline LayoutScale Make(const nlohmann::json& root) {
    LayoutScale scale;
    scale.enabled = root.value("scaleToWindow", false);
    if (root.contains("designResolution")) {
        scale.designResolution = ReadVector2(root["designResolution"], scale.designResolution);
    }
    scale.designResolution.x = AtLeastOne(scale.designResolution.x);
    scale.designResolution.y = AtLeastOne(scale.designResolution.y);
    // 縦横を別々に拡縮する仕様です。アスペクト比維持が必要なSpriteは呼び出し側で調整します。
    scale.ratio = {
        static_cast<float>(WinApp::kClientWidth) / scale.designResolution.x,
        static_cast<float>(WinApp::kClientHeight) / scale.designResolution.y
    };
    return scale;
}

inline Vector2 ScalePosition(const Vector2& value, const LayoutScale& scale) {
    return scale.enabled ? Vector2{ value.x * scale.ratio.x, value.y * scale.ratio.y } : value;
}

inline Vector2 ScaleSize(const Vector2& value, const LayoutScale& scale) {
    return scale.enabled ? Vector2{ value.x * scale.ratio.x, value.y * scale.ratio.y } : value;
}
}

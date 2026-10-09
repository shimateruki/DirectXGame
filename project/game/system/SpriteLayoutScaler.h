#pragma once

#include "Math.h"
#include "WinApp.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

// JSON の設計解像度から実画面への座標変換を行います。
namespace SpriteLayoutScaler {

// 有効時のみ ratio を適用し、既存 JSON の絶対座標もそのまま利用できます。
struct LayoutScale {
    Vector2 designResolution = {1920.0f, 1080.0f};
    Vector2 ratio = {1.0f, 1.0f};
    bool enabled = false;
};

// レイアウトの任意項目を型確認して読み、壊れた項目だけ既定値に戻します。
inline std::string ReadString(const nlohmann::json& object, const char* key, const std::string& fallback) {
    return object.is_object() && object.contains(key) && object[key].is_string()
               ? object[key].get<std::string>()
               : fallback;
}

inline bool ReadBool(const nlohmann::json& object, const char* key, bool fallback) {
    return object.is_object() && object.contains(key) && object[key].is_boolean() ? object[key].get<bool>() : fallback;
}

inline float ReadFloat(const nlohmann::json& object, const char* key, float fallback) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number()) {
        return fallback;
    }
    const float value = object[key].get<float>();
    return std::isfinite(value) ? value : fallback;
}

inline int ReadInt(const nlohmann::json& object, const char* key, int fallback) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number_integer()) {
        return fallback;
    }
    const auto& value = object[key];
    if (value.is_number_unsigned()) {
        const uint64_t number = value.get<uint64_t>();
        return number <= static_cast<uint64_t>((std::numeric_limits<int>::max)()) ? static_cast<int>(number)
                                                                                  : fallback;
    }
    const int64_t number = value.get<int64_t>();
    return number >= (std::numeric_limits<int>::min)() && number <= (std::numeric_limits<int>::max)()
               ? static_cast<int>(number)
               : fallback;
}

inline Vector2 ReadVector2(const nlohmann::json& value, const Vector2& fallback) {
    if (!value.is_array() || value.size() < 2 || !value[0].is_number() || !value[1].is_number()) {
        return fallback;
    }

    const Vector2 result = {value[0].get<float>(), value[1].get<float>()};
    return std::isfinite(result.x) && std::isfinite(result.y) ? result : fallback;
}

inline Vector4 ReadColor(const nlohmann::json& value, const Vector4& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    const size_t count = (std::min)(value.size(), static_cast<size_t>(4));
    for (size_t index = 0; index < count; ++index) {
        if (!value[index].is_number()) {
            return fallback;
        }
    }
    Vector4 color = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                     count == 4 ? value[3].get<float>() : fallback.w};
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z) && std::isfinite(color.w)
               ? color
               : fallback;
}

// サイズの分母に使う設計解像度を 1 以上に保ち、ゼロ除算を避けます。
inline float AtLeastOne(float value) { return std::isfinite(value) && value >= 1.0f ? value : 1.0f; }

// scaleToWindow が真の場合に限り、設計解像度から現在の画面比率を作ります。
inline LayoutScale Make(const nlohmann::json& root) {
    LayoutScale scale;
    scale.enabled = root.is_object() && root.contains("scaleToWindow") && root["scaleToWindow"].is_boolean()
                        ? root["scaleToWindow"].get<bool>()
                        : false;

    if (root.contains("designResolution")) {
        scale.designResolution = ReadVector2(root["designResolution"], scale.designResolution);
    }

    scale.designResolution.x = AtLeastOne(scale.designResolution.x);
    scale.designResolution.y = AtLeastOne(scale.designResolution.y);

    scale.ratio = {static_cast<float>(WinApp::kClientWidth) / scale.designResolution.x,
                   static_cast<float>(WinApp::kClientHeight) / scale.designResolution.y};

    return scale;
}

// 位置とサイズは X/Y を別々の倍率で伸縮します。縦横比は維持しません。
inline Vector2 ScalePosition(const Vector2& value, const LayoutScale& scale) {
    if (!scale.enabled) {
        return value;
    }

    return {value.x * scale.ratio.x, value.y * scale.ratio.y};
}

inline Vector2 ScaleSize(const Vector2& value, const LayoutScale& scale) {
    if (!scale.enabled) {
        return value;
    }

    return {value.x * scale.ratio.x, value.y * scale.ratio.y};
}

// JSON の有効フラグに関係なく設計座標を実画面へ変換します。
inline Vector2 ScaleDesignPosition(const Vector2& value, const Vector2& designResolution = {1920.0f, 1080.0f}) {
    LayoutScale scale;
    scale.enabled = true;
    scale.designResolution = designResolution;
    scale.designResolution.x = AtLeastOne(scale.designResolution.x);
    scale.designResolution.y = AtLeastOne(scale.designResolution.y);
    scale.ratio = {static_cast<float>(WinApp::kClientWidth) / scale.designResolution.x,
                   static_cast<float>(WinApp::kClientHeight) / scale.designResolution.y};
    return ScalePosition(value, scale);
}

inline Vector2 ScaleDesignSize(const Vector2& value, const Vector2& designResolution = {1920.0f, 1080.0f}) {
    LayoutScale scale;
    scale.enabled = true;
    scale.designResolution = designResolution;
    scale.designResolution.x = AtLeastOne(scale.designResolution.x);
    scale.designResolution.y = AtLeastOne(scale.designResolution.y);
    scale.ratio = {static_cast<float>(WinApp::kClientWidth) / scale.designResolution.x,
                   static_cast<float>(WinApp::kClientHeight) / scale.designResolution.y};
    return ScaleSize(value, scale);
}

inline float ScaleDesignX(float value, float designWidth = 1920.0f) {
    designWidth = AtLeastOne(designWidth);
    return value * static_cast<float>(WinApp::kClientWidth) / designWidth;
}

inline float ScaleDesignY(float value, float designHeight = 1080.0f) {
    designHeight = AtLeastOne(designHeight);
    return value * static_cast<float>(WinApp::kClientHeight) / designHeight;
}

} // namespace SpriteLayoutScaler

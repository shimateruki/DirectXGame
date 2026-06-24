#pragma once

#include "Math.h"
#include "WinApp.h"
#include "json.hpp"

#include <algorithm>

namespace SpriteLayoutScaler {

struct LayoutScale {
    Vector2 designResolution = { 1920.0f, 1080.0f };
    Vector2 ratio = { 1.0f, 1.0f };
    bool enabled = false;
};

inline Vector2 ReadVector2(const nlohmann::json& value, const Vector2& fallback) {
    if (!value.is_array() || value.size() < 2) {
        return fallback;
    }

    return {
        value[0].get<float>(),
        value[1].get<float>()
    };
}

inline float AtLeastOne(float value) {
    return value < 1.0f ? 1.0f : value;
}

inline LayoutScale Make(const nlohmann::json& root) {
    LayoutScale scale;
    scale.enabled = root.value("scaleToWindow", false);

    if (root.contains("designResolution")) {
        scale.designResolution = ReadVector2(root["designResolution"], scale.designResolution);
    }

    scale.designResolution.x = AtLeastOne(scale.designResolution.x);
    scale.designResolution.y = AtLeastOne(scale.designResolution.y);

    scale.ratio = {
        static_cast<float>(WinApp::kClientWidth) / scale.designResolution.x,
        static_cast<float>(WinApp::kClientHeight) / scale.designResolution.y
    };

    return scale;
}

inline Vector2 ScalePosition(const Vector2& value, const LayoutScale& scale) {
    if (!scale.enabled) {
        return value;
    }

    return {
        value.x * scale.ratio.x,
        value.y * scale.ratio.y
    };
}

inline Vector2 ScaleSize(const Vector2& value, const LayoutScale& scale) {
    if (!scale.enabled) {
        return value;
    }

    return {
        value.x * scale.ratio.x,
        value.y * scale.ratio.y
    };
}

inline Vector2 ScaleDesignPosition(const Vector2& value, const Vector2& designResolution = { 1920.0f, 1080.0f }) {
    LayoutScale scale;
    scale.enabled = true;
    scale.designResolution = designResolution;
    scale.designResolution.x = AtLeastOne(scale.designResolution.x);
    scale.designResolution.y = AtLeastOne(scale.designResolution.y);
    scale.ratio = {
        static_cast<float>(WinApp::kClientWidth) / scale.designResolution.x,
        static_cast<float>(WinApp::kClientHeight) / scale.designResolution.y
    };
    return ScalePosition(value, scale);
}

inline Vector2 ScaleDesignSize(const Vector2& value, const Vector2& designResolution = { 1920.0f, 1080.0f }) {
    LayoutScale scale;
    scale.enabled = true;
    scale.designResolution = designResolution;
    scale.designResolution.x = AtLeastOne(scale.designResolution.x);
    scale.designResolution.y = AtLeastOne(scale.designResolution.y);
    scale.ratio = {
        static_cast<float>(WinApp::kClientWidth) / scale.designResolution.x,
        static_cast<float>(WinApp::kClientHeight) / scale.designResolution.y
    };
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

}

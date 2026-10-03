#pragma once

#include "engine/utility/math/Math.h"

#include <algorithm>
#include <cmath>

namespace ColorSpace {

/// <summary>Editor の sRGB 入力を、GPU に渡す直前に変換するか管理します。</summary>
class WorkflowSettings {
public:
    /// <summary>共有する色空間設定を取得します。</summary>
    static WorkflowSettings& GetInstance() {
        static WorkflowSettings instance;
        return instance;
    }

    /// <summary>線形色空間への変換が有効か返します。</summary>
    bool IsLinearWorkflowEnabled() const { return linearWorkflowEnabled_; }

    /// <summary>線形色空間への変換を有効または無効にします。</summary>
    void SetLinearWorkflowEnabled(bool enabled) { linearWorkflowEnabled_ = enabled; }

private:
    WorkflowSettings() = default;
    bool linearWorkflowEnabled_ = false;
};

/// <summary>sRGB の単一チャンネルを線形値へ変換します。</summary>
inline float SRGBToLinearChannel(float value) {
    value = (std::max)(value, 0.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

/// <summary>RGB を sRGB から線形値へ変換します。</summary>
inline Vector3 SRGBToLinear(const Vector3& color) {
    return {
        SRGBToLinearChannel(color.x),
        SRGBToLinearChannel(color.y),
        SRGBToLinearChannel(color.z)
    };
}

/// <summary>RGB のみを線形値へ変換し、アルファはそのまま保持します。</summary>
inline Vector4 SRGBToLinear(const Vector4& color) {
    return {
        SRGBToLinearChannel(color.x),
        SRGBToLinearChannel(color.y),
        SRGBToLinearChannel(color.z),
        color.w
    };
}

/// <summary>現在の作業色空間に合わせて、Editor の RGB 入力を変換します。</summary>
inline Vector3 AuthoringToWorking(const Vector3& color) {
    return WorkflowSettings::GetInstance().IsLinearWorkflowEnabled()
        ? SRGBToLinear(color)
        : color;
}

/// <summary>現在の作業色空間に合わせて、Editor の RGBA 入力を変換します。</summary>
inline Vector4 AuthoringToWorking(const Vector4& color) {
    return WorkflowSettings::GetInstance().IsLinearWorkflowEnabled()
        ? SRGBToLinear(color)
        : color;
}

} // namespace ColorSpace

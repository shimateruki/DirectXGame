#pragma once

#include "engine/utility/math/Math.h"

#include <algorithm>
#include <cmath>

namespace ColorSpace {

// Editorで入力する色はsRGBのまま保持し、GPUへ渡す直前に作業色空間へ変換します。
class WorkflowSettings {
public:
    static WorkflowSettings& GetInstance() {
        static WorkflowSettings instance;
        return instance;
    }

    bool IsLinearWorkflowEnabled() const { return linearWorkflowEnabled_; }
    void SetLinearWorkflowEnabled(bool enabled) { linearWorkflowEnabled_ = enabled; }

private:
    WorkflowSettings() = default;
    bool linearWorkflowEnabled_ = false;
};

inline float SRGBToLinearChannel(float value) {
    value = (std::max)(value, 0.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline Vector3 SRGBToLinear(const Vector3& color) {
    return {
        SRGBToLinearChannel(color.x),
        SRGBToLinearChannel(color.y),
        SRGBToLinearChannel(color.z)
    };
}

inline Vector4 SRGBToLinear(const Vector4& color) {
    return {
        SRGBToLinearChannel(color.x),
        SRGBToLinearChannel(color.y),
        SRGBToLinearChannel(color.z),
        color.w
    };
}

inline Vector3 AuthoringToWorking(const Vector3& color) {
    return WorkflowSettings::GetInstance().IsLinearWorkflowEnabled()
        ? SRGBToLinear(color)
        : color;
}

inline Vector4 AuthoringToWorking(const Vector4& color) {
    return WorkflowSettings::GetInstance().IsLinearWorkflowEnabled()
        ? SRGBToLinear(color)
        : color;
}

} // namespace ColorSpace

#pragma once

#include "engine/utility/math/Math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

enum class VFXCurveEasing : int {
    Linear = 0,
    EaseIn = 1,
    EaseOut = 2,
    EaseInOut = 3,
    SmoothStep = 4,
};

inline float ApplyVFXCurveEasing(VFXCurveEasing easing, float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    switch (easing) {
    case VFXCurveEasing::EaseIn:
        return t * t;
    case VFXCurveEasing::EaseOut:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case VFXCurveEasing::EaseInOut:
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
    case VFXCurveEasing::SmoothStep:
        return t * t * (3.0f - 2.0f * t);
    case VFXCurveEasing::Linear:
    default:
        return t;
    }
}

struct VFXFloatCurveKey {
    float time = 0.0f;
    float value = 1.0f;
    VFXCurveEasing easing = VFXCurveEasing::Linear;
};

struct VFXColorGradientKey {
    float time = 0.0f;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    VFXCurveEasing easing = VFXCurveEasing::Linear;
};

inline bool VFXCurveKeyTimeLess(const VFXFloatCurveKey& lhs, const VFXFloatCurveKey& rhs) {
    return lhs.time < rhs.time;
}

inline bool VFXGradientKeyTimeLess(const VFXColorGradientKey& lhs, const VFXColorGradientKey& rhs) {
    return lhs.time < rhs.time;
}

class VFXFloatCurve {
public:
    std::vector<VFXFloatCurveKey> keys;

    void Normalize() {
        for (VFXFloatCurveKey& key : keys) {
            key.time = std::clamp(key.time, 0.0f, 1.0f);
        }
        std::stable_sort(keys.begin(), keys.end(), VFXCurveKeyTimeLess);
    }

    float Evaluate(float time, float fallback = 1.0f) const {
        if (keys.empty()) return fallback;
        if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
        if (time >= keys.back().time) return keys.back().value;

        for (std::size_t index = 1; index < keys.size(); ++index) {
            const VFXFloatCurveKey& right = keys[index];
            if (time > right.time) continue;
            const VFXFloatCurveKey& left = keys[index - 1];
            const float duration = (std::max)(right.time - left.time, 0.0001f);
            const float localTime = ApplyVFXCurveEasing(left.easing, (time - left.time) / duration);
            return std::lerp(left.value, right.value, localTime);
        }
        return keys.back().value;
    }

    static VFXFloatCurve FromLegacySamples(const float* values, std::size_t count) {
        VFXFloatCurve curve;
        if (!values || count == 0) return curve;
        curve.keys.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const float time = count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
            curve.keys.push_back({ time, values[index], VFXCurveEasing::Linear });
        }
        return curve;
    }

    static VFXFloatCurve FromThreePoints(
        float startValue,
        float middleTime,
        float middleValue,
        float endValue,
        VFXCurveEasing easing = VFXCurveEasing::Linear) {
        VFXFloatCurve curve;
        curve.keys = {
            { 0.0f, startValue, easing },
            { std::clamp(middleTime, 0.01f, 0.99f), middleValue, easing },
            { 1.0f, endValue, VFXCurveEasing::Linear },
        };
        return curve;
    }
};

class VFXColorGradient {
public:
    std::vector<VFXColorGradientKey> keys;

    void Normalize() {
        for (VFXColorGradientKey& key : keys) {
            key.time = std::clamp(key.time, 0.0f, 1.0f);
        }
        std::stable_sort(keys.begin(), keys.end(), VFXGradientKeyTimeLess);
    }

    Vector4 Evaluate(float time, const Vector4& fallback = { 1.0f, 1.0f, 1.0f, 1.0f }) const {
        if (keys.empty()) return fallback;
        if (keys.size() == 1 || time <= keys.front().time) return keys.front().color;
        if (time >= keys.back().time) return keys.back().color;

        for (std::size_t index = 1; index < keys.size(); ++index) {
            const VFXColorGradientKey& right = keys[index];
            if (time > right.time) continue;
            const VFXColorGradientKey& left = keys[index - 1];
            const float duration = (std::max)(right.time - left.time, 0.0001f);
            const float localTime = ApplyVFXCurveEasing(left.easing, (time - left.time) / duration);
            return {
                std::lerp(left.color.x, right.color.x, localTime),
                std::lerp(left.color.y, right.color.y, localTime),
                std::lerp(left.color.z, right.color.z, localTime),
                std::lerp(left.color.w, right.color.w, localTime),
            };
        }
        return keys.back().color;
    }

    static VFXColorGradient FromTwoColors(const Vector4& startColor, const Vector4& endColor) {
        VFXColorGradient gradient;
        gradient.keys = {
            { 0.0f, startColor, VFXCurveEasing::Linear },
            { 1.0f, endColor, VFXCurveEasing::Linear },
        };
        return gradient;
    }

    static VFXColorGradient FromThreeColors(
        const Vector4& startColor,
        float middleTime,
        const Vector4& middleColor,
        const Vector4& endColor,
        VFXCurveEasing easing = VFXCurveEasing::Linear) {
        VFXColorGradient gradient;
        gradient.keys = {
            { 0.0f, startColor, easing },
            { std::clamp(middleTime, 0.01f, 0.99f), middleColor, easing },
            { 1.0f, endColor, VFXCurveEasing::Linear },
        };
        return gradient;
    }
};

inline constexpr std::size_t kVFXBakedSampleCount = 8;

struct VFXBakedFloatCurve {
    std::array<float, kVFXBakedSampleCount> samples{};

    float Evaluate(float time) const {
        const float scaled = std::clamp(time, 0.0f, 1.0f) * static_cast<float>(samples.size() - 1);
        const std::size_t left = static_cast<std::size_t>(scaled);
        const std::size_t right = (std::min)(left + 1, samples.size() - 1);
        return std::lerp(samples[left], samples[right], scaled - static_cast<float>(left));
    }
};

struct VFXBakedColorGradient {
    std::array<Vector4, kVFXBakedSampleCount> samples{};

    Vector4 Evaluate(float time) const {
        const float scaled = std::clamp(time, 0.0f, 1.0f) * static_cast<float>(samples.size() - 1);
        const std::size_t left = static_cast<std::size_t>(scaled);
        const std::size_t right = (std::min)(left + 1, samples.size() - 1);
        const float ratio = scaled - static_cast<float>(left);
        return {
            std::lerp(samples[left].x, samples[right].x, ratio),
            std::lerp(samples[left].y, samples[right].y, ratio),
            std::lerp(samples[left].z, samples[right].z, ratio),
            std::lerp(samples[left].w, samples[right].w, ratio),
        };
    }
};

inline VFXBakedFloatCurve BakeVFXCurve(const VFXFloatCurve& curve, float fallback = 1.0f) {
    VFXBakedFloatCurve baked;
    for (std::size_t index = 0; index < baked.samples.size(); ++index) {
        baked.samples[index] = curve.Evaluate(
            static_cast<float>(index) / static_cast<float>(baked.samples.size() - 1),
            fallback);
    }
    return baked;
}

inline VFXBakedColorGradient BakeVFXGradient(
    const VFXColorGradient& gradient,
    const Vector4& fallback = { 1.0f, 1.0f, 1.0f, 1.0f }) {
    VFXBakedColorGradient baked;
    for (std::size_t index = 0; index < baked.samples.size(); ++index) {
        baked.samples[index] = gradient.Evaluate(
            static_cast<float>(index) / static_cast<float>(baked.samples.size() - 1),
            fallback);
    }
    return baked;
}

struct VFXLodSettings {
    bool enabled = false;
    float nearDistance = 12.0f;
    float farDistance = 45.0f;
    float farEmissionScale = 0.25f;
    int maxAliveParticles = 0;

    void Sanitize() {
        nearDistance = (std::max)(nearDistance, 0.0f);
        farDistance = (std::max)(farDistance, nearDistance + 0.01f);
        farEmissionScale = std::clamp(farEmissionScale, 0.0f, 1.0f);
        maxAliveParticles = (std::max)(maxAliveParticles, 0);
    }

    float EvaluateEmissionScale(float distance) const {
        if (!enabled || distance <= nearDistance) return 1.0f;
        if (distance >= farDistance) return std::clamp(farEmissionScale, 0.0f, 1.0f);
        const float range = (std::max)(farDistance - nearDistance, 0.01f);
        const float ratio = (distance - nearDistance) / range;
        return std::lerp(1.0f, std::clamp(farEmissionScale, 0.0f, 1.0f), ratio);
    }
};

#pragma once

#include "engine/utility/math/Math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

/// <summary>キー間を補間する際の時間の進み方。</summary>
enum class VFXCurveEasing : int {
    Linear = 0,
    EaseIn = 1,
    EaseOut = 2,
    EaseInOut = 3,
    SmoothStep = 4,
};

/// <summary>0～1の入力を指定のイージングで変換する。</summary>
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

/// <summary>寿命に対する数値カーブのキー。timeは0～1の再生割合。</summary>
struct VFXFloatCurveKey {
    float time = 0.0f;
    float value = 1.0f;
    VFXCurveEasing easing = VFXCurveEasing::Linear;
};

/// <summary>寿命に対する色グラデーションのキー。timeは0～1の再生割合。</summary>
struct VFXColorGradientKey {
    float time = 0.0f;
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    VFXCurveEasing easing = VFXCurveEasing::Linear;
};

/// <summary>数値カーブのキーを時刻順に並べるための比較関数。</summary>
inline bool VFXCurveKeyTimeLess(const VFXFloatCurveKey& lhs, const VFXFloatCurveKey& rhs) {
    return lhs.time < rhs.time;
}

/// <summary>色グラデーションのキーを時刻順に並べるための比較関数。</summary>
inline bool VFXGradientKeyTimeLess(const VFXColorGradientKey& lhs, const VFXColorGradientKey& rhs) {
    return lhs.time < rhs.time;
}

/// <summary>パーティクル寿命に沿って数値を補間する編集用カーブ。</summary>
class VFXFloatCurve {
public:
    std::vector<VFXFloatCurveKey> keys;

    /// <summary>キー時刻を有効範囲へ収め、時刻順に安定ソートする。</summary>
    void Normalize() {
        for (VFXFloatCurveKey& key : keys) {
            key.time = std::clamp(key.time, 0.0f, 1.0f);
        }
        std::stable_sort(keys.begin(), keys.end(), VFXCurveKeyTimeLess);
    }

    /// <summary>指定した再生割合で値を評価する。キーがなければfallbackを返す。</summary>
    /// <remarks>区間の補間には左側キーのイージングを使う。</remarks>
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

    /// <summary>旧形式の等間隔サンプルを編集用キーへ変換する。</summary>
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

    /// <summary>開始・中間・終了の値から三点カーブを作る。</summary>
    /// <param name="middleTime">中間キーの寿命割合。0.01～0.99に収められる。</param>
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

/// <summary>パーティクル寿命に沿って色を補間する編集用グラデーション。</summary>
class VFXColorGradient {
public:
    std::vector<VFXColorGradientKey> keys;

    /// <summary>キー時刻を有効範囲へ収め、時刻順に安定ソートする。</summary>
    void Normalize() {
        for (VFXColorGradientKey& key : keys) {
            key.time = std::clamp(key.time, 0.0f, 1.0f);
        }
        std::stable_sort(keys.begin(), keys.end(), VFXGradientKeyTimeLess);
    }

    /// <summary>指定した再生割合で色を評価する。キーがなければfallbackを返す。</summary>
    /// <remarks>区間の補間には左側キーのイージングを使う。</remarks>
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

    /// <summary>開始色と終了色を線形補間するグラデーションを作る。</summary>
    static VFXColorGradient FromTwoColors(const Vector4& startColor, const Vector4& endColor) {
        VFXColorGradient gradient;
        gradient.keys = {
            { 0.0f, startColor, VFXCurveEasing::Linear },
            { 1.0f, endColor, VFXCurveEasing::Linear },
        };
        return gradient;
    }

    /// <summary>開始・中間・終了の三色を補間するグラデーションを作る。</summary>
    /// <param name="middleTime">中間キーの寿命割合。0.01～0.99に収められる。</param>
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

// 実行時の評価コストを抑えるため、編集用カーブを固定数サンプルに焼き込む。
inline constexpr std::size_t kVFXBakedSampleCount = 8;

/// <summary>実行時に線形補間する数値カーブの固定サンプル。</summary>
struct VFXBakedFloatCurve {
    std::array<float, kVFXBakedSampleCount> samples{};

    /// <summary>寿命割合から固定サンプル間を補間し、数値を返す。</summary>
    float Evaluate(float time) const {
        const float scaled = std::clamp(time, 0.0f, 1.0f) * static_cast<float>(samples.size() - 1);
        const std::size_t left = static_cast<std::size_t>(scaled);
        const std::size_t right = (std::min)(left + 1, samples.size() - 1);
        return std::lerp(samples[left], samples[right], scaled - static_cast<float>(left));
    }
};

/// <summary>実行時に線形補間する色グラデーションの固定サンプル。</summary>
struct VFXBakedColorGradient {
    std::array<Vector4, kVFXBakedSampleCount> samples{};

    /// <summary>寿命割合から固定サンプル間を補間し、色を返す。</summary>
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

/// <summary>編集用数値カーブを実行時に使う固定サンプルへ焼き込む。</summary>
inline VFXBakedFloatCurve BakeVFXCurve(const VFXFloatCurve& curve, float fallback = 1.0f) {
    VFXBakedFloatCurve baked;
    for (std::size_t index = 0; index < baked.samples.size(); ++index) {
        baked.samples[index] = curve.Evaluate(
            static_cast<float>(index) / static_cast<float>(baked.samples.size() - 1),
            fallback);
    }
    return baked;
}

/// <summary>編集用色グラデーションを実行時に使う固定サンプルへ焼き込む。</summary>
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

/// <summary>距離に応じて粒子の発生数と生存上限を調整する設定。</summary>
struct VFXLodSettings {
    bool enabled = false;
    float nearDistance = 12.0f;
    float farDistance = 45.0f;
    float farEmissionScale = 0.25f;
    int maxAliveParticles = 0;

    /// <summary>距離・発生倍率・粒子上限を有効な範囲へ正規化する。</summary>
    void Sanitize() {
        nearDistance = (std::max)(nearDistance, 0.0f);
        farDistance = (std::max)(farDistance, nearDistance + 0.01f);
        farEmissionScale = std::clamp(farEmissionScale, 0.0f, 1.0f);
        maxAliveParticles = (std::max)(maxAliveParticles, 0);
    }

    /// <summary>指定距離で使用する発生倍率を返す。</summary>
    float EvaluateEmissionScale(float distance) const {
        if (!enabled || distance <= nearDistance) return 1.0f;
        if (distance >= farDistance) return std::clamp(farEmissionScale, 0.0f, 1.0f);
        const float range = (std::max)(farDistance - nearDistance, 0.01f);
        const float ratio = (distance - nearDistance) / range;
        return std::lerp(1.0f, std::clamp(farEmissionScale, 0.0f, 1.0f), ratio);
    }
};

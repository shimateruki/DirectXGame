#pragma once

#include "engine/utility/math/Math.h"
#include "Easing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

/// <summary>キー間を補間する際の時間の進み方。</summary>
enum class VfxCurveEasing : int {
    Linear = 0,
    EaseIn = 1,
    EaseOut = 2,
    EaseInOut = 3,
    SmoothStep = 4,
};

/// <summary>0～1の入力を指定のイージングで変換する。</summary>
inline float ApplyVfxCurveEasing(VfxCurveEasing easing, float value) {
    // 補間割合を先に0～1へ収め、曲線ごとの式が区間外へはみ出さないようにする。
    const float t = std::clamp(value, 0.0f, 1.0f);
    switch (easing) {
    case VfxCurveEasing::EaseIn:
        return t * t;
    case VfxCurveEasing::EaseOut:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case VfxCurveEasing::EaseInOut:
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
    case VfxCurveEasing::SmoothStep:
        return Easing::SmoothStep(t);
    case VfxCurveEasing::Linear:
    default:
        return t;
    }
}

/// <summary>寿命に対する数値カーブのキー。timeは0～1の再生割合。</summary>
// 関連：VfxFloatCurve（この型を使う機能の本体）
struct VfxFloatCurveKey {
    float time = 0.0f; // 寿命に対する位置。0が発生、1が末尾。
    float value = 1.0f;
    VfxCurveEasing easing = VfxCurveEasing::Linear; // 次キーへの補間方式。
};

/// <summary>寿命に対する色グラデーションのキー。timeは0～1の再生割合。</summary>
// 関連：VfxColorGradient（色の補間）
struct VfxColorGradientKey {
    float time = 0.0f; // 寿命に対する位置。0が発生、1が末尾。
    engine::Vector4 color = engine::Colors::White;
    VfxCurveEasing easing = VfxCurveEasing::Linear; // 次キーへの補間方式。
};

/// <summary>数値カーブのキーを時刻順に並べるための比較関数。</summary>
inline bool VfxCurveKeyTimeLess(const VfxFloatCurveKey& lhs, const VfxFloatCurveKey& rhs) {
    return lhs.time < rhs.time;
}

/// <summary>色グラデーションのキーを時刻順に並べるための比較関数。</summary>
inline bool VfxGradientKeyTimeLess(const VfxColorGradientKey& lhs, const VfxColorGradientKey& rhs) {
    return lhs.time < rhs.time;
}

// 担当：粒子寿命に対する数値変化をキーで編集・補間する。
// 関連：VfxFloatCurveKey（キー）、VfxBakedFloatCurve（実行用サンプル）
// 主な入口：Normalize / Evaluate / FromLegacySamples
class VfxFloatCurve {
public:
    std::vector<VfxFloatCurveKey> keys;

    /// <summary>キー時刻を有効範囲へ収め、時刻順に安定ソートする。</summary>
    void Normalize() {
        for (VfxFloatCurveKey& key : keys) {
            key.time = std::clamp(key.time, 0.0f, 1.0f);
        }
        std::stable_sort(keys.begin(), keys.end(), VfxCurveKeyTimeLess);
    }

    /// <summary>指定した再生割合で値を評価する。キーがなければfallbackを返す。</summary>
    /// <remarks>keysはNormalize等で昇順にしておく。範囲外は端の値、区間の補間には左側キーのイージングを使う。</remarks>
    float Evaluate(float time, float fallback = 1.0f) const {
        // 左側キーのイージングを、そのキーから次のキーへ向かう区間に適用する。
        if (keys.empty())
            return fallback;
        if (keys.size() == 1 || time <= keys.front().time)
            return keys.front().value;
        if (time >= keys.back().time)
            return keys.back().value;

        for (std::size_t index = 1; index < keys.size(); ++index) {
            const VfxFloatCurveKey& right = keys[index];
            if (time > right.time)
                continue;
            const VfxFloatCurveKey& left = keys[index - 1];
            const float duration = (std::max)(right.time - left.time, 0.0001f);
            const float localTime = ApplyVfxCurveEasing(left.easing, (time - left.time) / duration);
            return std::lerp(left.value, right.value, localTime);
        }
        return keys.back().value;
    }

    /// <summary>旧形式の等間隔サンプルを編集用キーへ変換する。</summary>
    static VfxFloatCurve FromLegacySamples(const float* values, std::size_t count) {
        VfxFloatCurve curve;
        if (!values || count == 0)
            return curve;
        curve.keys.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const float time = count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
            curve.keys.push_back({ time, values[index], VfxCurveEasing::Linear });
        }
        return curve;
    }

    /// <summary>開始・中間・終了の値から三点カーブを作る。</summary>
    /// <param name="middleTime">中間キーの寿命割合。0.01～0.99に収められる。</param>
    static VfxFloatCurve FromThreePoints(
        float startValue,
        float middleTime,
        float middleValue,
        float endValue,
        VfxCurveEasing easing = VfxCurveEasing::Linear) {
        VfxFloatCurve curve;
        curve.keys = {
            { 0.0f, startValue, easing },
            { std::clamp(middleTime, 0.01f, 0.99f), middleValue, easing },
            { 1.0f, endValue, VfxCurveEasing::Linear },
        };
        return curve;
    }
};

// 担当：粒子寿命に対する色変化をキーで編集・補間する。
// 関連：VfxColorGradientKey（キー）、VfxBakedColorGradient（実行用サンプル）
// 主な入口：Normalize / Evaluate / FromTwoColors
class VfxColorGradient {
public:
    std::vector<VfxColorGradientKey> keys;

    /// <summary>キー時刻を有効範囲へ収め、時刻順に安定ソートする。</summary>
    void Normalize() {
        for (VfxColorGradientKey& key : keys) {
            key.time = std::clamp(key.time, 0.0f, 1.0f);
        }
        std::stable_sort(keys.begin(), keys.end(), VfxGradientKeyTimeLess);
    }

    /// <summary>指定した再生割合で色を評価する。キーがなければfallbackを返す。</summary>
    /// <remarks>keysはNormalize等で昇順にしておく。RGBA各成分を補間し、色空間変換は行わない。</remarks>
    engine::Vector4 Evaluate(float time, const engine::Vector4& fallback = { 1.0f, 1.0f, 1.0f, 1.0f }) const {
        // RGBAを同じ時刻割合で補間し、キーがない場合だけ呼び出し側の色へ戻す。
        if (keys.empty())
            return fallback;
        if (keys.size() == 1 || time <= keys.front().time)
            return keys.front().color;
        if (time >= keys.back().time)
            return keys.back().color;

        for (std::size_t index = 1; index < keys.size(); ++index) {
            const VfxColorGradientKey& right = keys[index];
            if (time > right.time)
                continue;
            const VfxColorGradientKey& left = keys[index - 1];
            const float duration = (std::max)(right.time - left.time, 0.0001f);
            const float localTime = ApplyVfxCurveEasing(left.easing, (time - left.time) / duration);
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
    static VfxColorGradient FromTwoColors(const engine::Vector4& startColor, const engine::Vector4& endColor) {
        VfxColorGradient gradient;
        gradient.keys = {
            { 0.0f, startColor, VfxCurveEasing::Linear },
            { 1.0f, endColor, VfxCurveEasing::Linear },
        };
        return gradient;
    }

    /// <summary>開始・中間・終了の三色を補間するグラデーションを作る。</summary>
    /// <param name="middleTime">中間キーの寿命割合。0.01～0.99に収められる。</param>
    static VfxColorGradient FromThreeColors(
        const engine::Vector4& startColor,
        float middleTime,
        const engine::Vector4& middleColor,
        const engine::Vector4& endColor,
        VfxCurveEasing easing = VfxCurveEasing::Linear) {
        VfxColorGradient gradient;
        gradient.keys = {
            { 0.0f, startColor, easing },
            { std::clamp(middleTime, 0.01f, 0.99f), middleColor, easing },
            { 1.0f, endColor, VfxCurveEasing::Linear },
        };
        return gradient;
    }
};

// 実行時の評価コストを抑えるため、編集用カーブを固定数サンプルに焼き込む。
inline constexpr std::size_t kVfxBakedSampleCount = 8;

/// <summary>実行時に線形補間する数値カーブの固定サンプル。</summary>
// 関連：VfxFloatCurve（編集元）、粒子システム（実行用サンプル）
struct VfxBakedFloatCurve {
    std::array<float, kVfxBakedSampleCount> samples{};

    /// <summary>寿命割合から固定サンプル間を補間し、数値を返す。</summary>
    float Evaluate(float time) const {
        const float scaled = std::clamp(time, 0.0f, 1.0f) * static_cast<float>(samples.size() - 1);
        const std::size_t left = static_cast<std::size_t>(scaled);
        const std::size_t right = (std::min)(left + 1, samples.size() - 1);
        return std::lerp(samples[left], samples[right], scaled - static_cast<float>(left));
    }
};

/// <summary>実行時に線形補間する色グラデーションの固定サンプル。</summary>
// 関連：VfxColorGradient（編集元）、粒子システム（実行用サンプル）
struct VfxBakedColorGradient {
    std::array<engine::Vector4, kVfxBakedSampleCount> samples{};

    /// <summary>寿命割合から固定サンプル間を補間し、色を返す。</summary>
    engine::Vector4 Evaluate(float time) const {
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
inline VfxBakedFloatCurve BakeVfxCurve(const VfxFloatCurve& curve, float fallback = 1.0f) {
    VfxBakedFloatCurve baked;
    for (std::size_t index = 0; index < baked.samples.size(); ++index) {
        baked.samples[index] = curve.Evaluate(
            static_cast<float>(index) / static_cast<float>(baked.samples.size() - 1),
            fallback);
    }
    return baked;
}

/// <summary>編集用色グラデーションを実行時に使う固定サンプルへ焼き込む。</summary>
inline VfxBakedColorGradient BakeVfxGradient(
    const VfxColorGradient& gradient,
    const engine::Vector4& fallback = { 1.0f, 1.0f, 1.0f, 1.0f }) {
    VfxBakedColorGradient baked;
    for (std::size_t index = 0; index < baked.samples.size(); ++index) {
        baked.samples[index] = gradient.Evaluate(
            static_cast<float>(index) / static_cast<float>(baked.samples.size() - 1),
            fallback);
    }
    return baked;
}

/// <summary>距離に応じて粒子の発生数と生存上限を調整する設定。</summary>
// 関連：GpuParticleConfig / ParticleSystem（距離による発生量と上限の調整）
struct VfxLodSettings {
    bool enabled = false;
    float nearDistance = 12.0f; // 発生数を減らし始めるカメラ距離。
    float farDistance = 45.0f; // 遠距離用の発生倍率へ到達する距離。
    float farEmissionScale = 0.25f; // 遠距離で適用する発生数の倍率。
    int maxAliveParticles = 0; // 0は個別の生存数上限を指定しない。

    /// <summary>距離・発生倍率・粒子上限を有効な範囲へ正規化する。</summary>
    void Sanitize() {
        nearDistance = (std::max)(nearDistance, 0.0f);
        farDistance = (std::max)(farDistance, nearDistance + 0.01f);
        farEmissionScale = std::clamp(farEmissionScale, 0.0f, 1.0f);
        maxAliveParticles = (std::max)(maxAliveParticles, 0);
    }

    /// <summary>指定距離で使用する発生倍率を返す。</summary>
    float EvaluateEmissionScale(float distance) const {
        if (!enabled || distance <= nearDistance)
            return 1.0f;
        if (distance >= farDistance)
            return std::clamp(farEmissionScale, 0.0f, 1.0f);
        const float range = (std::max)(farDistance - nearDistance, 0.01f);
        const float ratio = (distance - nearDistance) / range;
        return std::lerp(1.0f, std::clamp(farEmissionScale, 0.0f, 1.0f), ratio);
    }
};

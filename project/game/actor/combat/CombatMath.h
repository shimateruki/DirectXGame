#pragma once

#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cmath>

// 攻撃側とコピー能力で共有する、状態を持たない計算。
// 微小方向の扱いと補間式は既存の戦闘挙動を維持します。
namespace CombatMath {
// 入力の Y を捨てて XZ 平面で正規化します。長さがほぼゼロなら前方向 +Z を返します。
inline Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {value.x / length, 0.0f, value.z / length};
}

inline float PlanarDistance(const Vector3& lhs, const Vector3& rhs) {
    const float x = lhs.x - rhs.x;
    const float z = lhs.z - rhs.z;
    return std::sqrt(x * x + z * z);
}

inline float SmoothStep01(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

inline float EaseOutCubic(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    const float inverse = 1.0f - value;
    return 1.0f - inverse * inverse * inverse;
}

inline Vector3 LerpVector3(const Vector3& start, const Vector3& end, float rate) {
    return {
        Math::Lerp(start.x, end.x, rate),
        Math::Lerp(start.y, end.y, rate),
        Math::Lerp(start.z, end.z, rate),
    };
}

// toTarget は XZ 平面の正規化方向、distance と near/farDistance は同じワールド距離単位です。
// 間合い内では cycleSeconds 秒ごとに横移動と静止を交互に挟みます。
inline Vector3 MakePacedSpacingVelocity(const Vector3& toTarget, float distance, float nearDistance, float farDistance,
                                        float speed, float retreatRate, float strafeRate, float elapsed,
                                        float cycleSeconds) {
    if (distance > farDistance) {
        return {toTarget.x * speed, 0.0f, toTarget.z * speed};
    }
    if (distance < nearDistance) {
        return {-toTarget.x * speed * retreatRate, 0.0f, -toTarget.z * speed * retreatRate};
    }
    const float phase =
        std::fmod((std::max)(elapsed, 0.0f), (std::max)(cycleSeconds, 0.1f)) / (std::max)(cycleSeconds, 0.1f);
    const float side = phase < 0.38f ? 1.0f : (phase < 0.50f ? 0.0f : (phase < 0.88f ? -1.0f : 0.0f));
    const float band = SmoothStep01((std::min)(distance - nearDistance, farDistance - distance) / 1.2f);
    return {toTarget.z * speed * strafeRate * side * band, 0.0f, -toTarget.x * speed * strafeRate * side * band};
}

// deltaTime は秒、followRate は 1/秒です。Y 成分は補間せず要求値をそのまま使います。
inline Vector3 SmoothPlanarVelocity(const Vector3& previous, const Vector3& requested, float deltaTime,
                                    float followRate) {
    const float blend = 1.0f - std::exp(-(std::max)(deltaTime, 0.0f) * followRate);
    return {Math::Lerp(previous.x, requested.x, blend), requested.y, Math::Lerp(previous.z, requested.z, blend)};
}
} // namespace CombatMath

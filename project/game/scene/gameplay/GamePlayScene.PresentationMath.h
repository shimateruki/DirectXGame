#pragma once
#include "engine/utility/math/Math.h"
#include <cmath>

// 演出用の方向計算のみを共有する。状態・所有権は持たず、引数と計算式を維持する。
namespace GamePlayPresentationMath {
inline engine::Vector3 NormalizePlanarDirection(const engine::Vector3& direction, const engine::Vector3& fallback) {
    engine::Vector3 result = {direction.x, 0.0f, direction.z};
    const float length = std::sqrt(result.x * result.x + result.z * result.z);
    if (length <= 0.0001f) {
        return fallback;
    }
    result.x /= length;
    result.z /= length;
    return result;
}

inline engine::Vector3 MakeLookAtEuler(const engine::Vector3& eye, const engine::Vector3& target, float roll = 0.0f) {
    // 水平方向の距離を使って上下角を求め、シネマティック用のオイラー角へ変換する。
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const float dz = target.z - eye.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    return {std::atan2(-dy, horizontal), std::atan2(dx, dz), roll};
}

} // namespace GamePlayPresentationMath

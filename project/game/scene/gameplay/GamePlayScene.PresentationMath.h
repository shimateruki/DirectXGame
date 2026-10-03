#pragma once
#include "engine/utility/math/Math.h"
#include <cmath>

// 演出用の方向計算のみを共有する。状態・所有権は持たず、引数と計算式を維持する。
namespace GamePlayPresentationMath {
inline Vector3 NormalizePlanarDirection(const Vector3& direction, const Vector3& fallback) {
    Vector3 result = { direction.x, 0.0f, direction.z };
    const float length = std::sqrt(result.x * result.x + result.z * result.z);
    if (length <= 0.0001f) {
        return fallback;
    }
    result.x /= length;
    result.z /= length;
    return result;
}

inline Vector3 MakeLookAtEuler(const Vector3& eye, const Vector3& target, float roll = 0.0f) {
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const float dz = target.z - eye.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    return {
        std::atan2(-dy, horizontal),
        std::atan2(dx, dz),
        roll
    };
}

}

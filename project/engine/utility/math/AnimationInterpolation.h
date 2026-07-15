#pragma once

#include "engine/utility/math/Math.h"

#include <algorithm>
#include <cmath>

// キャラクター、カメラ、演出オブジェクトで共通利用する補間関数です。
// フレームレートに依存しない追従と、回転の最短経路補間を一か所へまとめます。
namespace AnimationInterpolation {

enum class EasingType {
    Linear = 0,
    EaseIn = 1,
    EaseOut = 2,
    EaseInOut = 3,
    SmootherStep = 4
};

inline float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline float ApplyEasing(float value, EasingType type) {
    const float t = Clamp01(value);
    switch (type) {
    case EasingType::EaseIn:
        return t * t * t;
    case EasingType::EaseOut: {
        const float inv = 1.0f - t;
        return 1.0f - inv * inv * inv;
    }
    case EasingType::EaseInOut:
        return t < 0.5f
            ? 4.0f * t * t * t
            : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    case EasingType::SmootherStep:
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    case EasingType::Linear:
    default:
        return t;
    }
}

inline float Lerp(float start, float end, float rate) {
    return start + (end - start) * Clamp01(rate);
}

inline Vector3 Lerp(const Vector3& start, const Vector3& end, float rate) {
    const float t = Clamp01(rate);
    return {
        start.x + (end.x - start.x) * t,
        start.y + (end.y - start.y) * t,
        start.z + (end.z - start.z) * t
    };
}

inline float FollowAlpha(float response, float deltaTime) {
    if (deltaTime <= 0.0f || response <= 0.0f) {
        return 0.0f;
    }
    return 1.0f - std::exp(-response * deltaTime);
}

inline Vector3 Damp(const Vector3& current, const Vector3& target, float response, float deltaTime) {
    return Lerp(current, target, FollowAlpha(response, deltaTime));
}

inline Vector3 SlerpEuler(const Vector3& startEuler, const Vector3& endEuler, float rate) {
    const Quaternion start = Math::EulerToQuaternion(startEuler);
    const Quaternion end = Math::EulerToQuaternion(endEuler);
    const Quaternion result = Math::Slerp(start, end, Clamp01(rate));
    return Math::MatrixToEuler(Math::MakeRotateQuaternionMatrix(result));
}

inline Vector3 DampEuler(const Vector3& currentEuler, const Vector3& targetEuler, float response, float deltaTime) {
    return SlerpEuler(currentEuler, targetEuler, FollowAlpha(response, deltaTime));
}

inline float SegmentRate(float time, float startTime, float endTime) {
    const float duration = endTime - startTime;
    if (duration <= 0.0001f) {
        return time >= endTime ? 1.0f : 0.0f;
    }
    return Clamp01((time - startTime) / duration);
}

} // namespace AnimationInterpolation

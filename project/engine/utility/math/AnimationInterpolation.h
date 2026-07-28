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

// Euler角の単一軸を最短経路で補間します。
// Y=180度付近でも0度側へ大回りしないため、キャラクターの旋回に使用できます。
inline float LerpAngle(float start, float end, float rate) {
    return Math::LerpShortAngle(start, end, Clamp01(rate));
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

inline float DampAngle(float current, float target, float response, float deltaTime) {
    return LerpAngle(current, target, FollowAlpha(response, deltaTime));
}

inline Vector3 Damp(const Vector3& current, const Vector3& target, float response, float deltaTime) {
    return Lerp(current, target, FollowAlpha(response, deltaTime));
}

// 各Euler軸を独立して最短経路補間します。
// 小さな傾きとキャラクターのY旋回を混ぜる用途では、クォータニオンを
// Euler角へ戻した際に起きる等価角度への折り返しを避けられます。
inline Vector3 LerpEulerAxes(const Vector3& startEuler, const Vector3& endEuler, float rate) {
    const float t = Clamp01(rate);
    return {
        LerpAngle(startEuler.x, endEuler.x, t),
        LerpAngle(startEuler.y, endEuler.y, t),
        LerpAngle(startEuler.z, endEuler.z, t)
    };
}

inline Vector3 DampEulerAxes(
    const Vector3& currentEuler,
    const Vector3& targetEuler,
    float response,
    float deltaTime) {
    return LerpEulerAxes(currentEuler, targetEuler, FollowAlpha(response, deltaTime));
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

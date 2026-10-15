#pragma once

#include "engine/utility/math/Math.h"

#include <algorithm>
#include <cmath>

// キャラクター、カメラ、演出オブジェクトで共通利用する補間関数です。
// フレームレートに依存しない追従と、回転の最短経路補間を一か所へまとめます。
namespace AnimationInterpolation {

/// <summary>補間率へ適用する加減速カーブの種類。</summary>
enum class EasingType {
    Linear = 0,
    EaseIn = 1,
    EaseOut = 2,
    EaseInOut = 3,
    SmootherStep = 4
};

/// <summary>補間率を 0～1 に収めます。</summary>
inline float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

/// <summary>0～1 の再生割合へ指定のイージングを適用します。</summary>
/// <param name="value">元の補間率。範囲外は 0～1 に収めます。</param>
/// <param name="type">イージングの種類。</param>
/// <returns>変換後の補間率。</returns>
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

/// <summary>2 つの数値を線形補間します。rate は 0～1 に収めます。</summary>
inline float Lerp(float start, float end, float rate) {
    return start + (end - start) * Clamp01(rate);
}

/// <summary>Euler 角の単一軸を最短経路で補間します。</summary>
/// <remarks>180 度付近でも遠回りしないため、キャラクターの旋回に使用できます。</remarks>
inline float LerpAngle(float start, float end, float rate) {
    return engine::Math::LerpShortAngle(start, end, Clamp01(rate));
}

/// <summary>2 つの座標を線形補間します。rate は 0～1 に収めます。</summary>
inline engine::Vector3 Lerp(const engine::Vector3& start, const engine::Vector3& end, float rate) {
    const float t = Clamp01(rate);
    return {
        start.x + (end.x - start.x) * t,
        start.y + (end.y - start.y) * t,
        start.z + (end.z - start.z) * t
    };
}

/// <summary>時間刻みに依存しない追従率を計算します。</summary>
/// <param name="response">追従の速さ。0 以下なら停止します。</param>
/// <param name="deltaTime">経過秒数。0 以下なら停止します。</param>
/// <returns>このフレームに適用する 0～1 の割合。</returns>
inline float FollowAlpha(float response, float deltaTime) {
    if (deltaTime <= 0.0f || response <= 0.0f) {
        return 0.0f;
    }
    return 1.0f - std::exp(-response * deltaTime);
}

/// <summary>単一軸の角度を最短経路で追従させます。</summary>
inline float DampAngle(float current, float target, float response, float deltaTime) {
    return LerpAngle(current, target, FollowAlpha(response, deltaTime));
}

/// <summary>座標を時間刻みに依存しない速度で追従させます。</summary>
inline engine::Vector3 Damp(const engine::Vector3& current, const engine::Vector3& target, float response, float deltaTime) {
    return Lerp(current, target, FollowAlpha(response, deltaTime));
}

/// <summary>Euler の各軸を独立に最短経路で補間します。</summary>
/// <remarks>小さな傾きと Y 旋回を混ぜる場合、Quaternion から Euler へ戻す際の折り返しを避けられます。</remarks>
inline engine::Vector3 LerpEulerAxes(const engine::Vector3& startEuler, const engine::Vector3& endEuler, float rate) {
    const float t = Clamp01(rate);
    return {
        LerpAngle(startEuler.x, endEuler.x, t),
        LerpAngle(startEuler.y, endEuler.y, t),
        LerpAngle(startEuler.z, endEuler.z, t)
    };
}

/// <summary>Euler の各軸を独立に、時間刻みに依存しない速度で追従させます。</summary>
inline engine::Vector3 DampEulerAxes(
    const engine::Vector3& currentEuler,
    const engine::Vector3& targetEuler,
    float response,
    float deltaTime) {
    return LerpEulerAxes(currentEuler, targetEuler, FollowAlpha(response, deltaTime));
}

/// <summary>Euler 角を Quaternion に変換して球面線形補間します。</summary>
inline engine::Vector3 SlerpEuler(const engine::Vector3& startEuler, const engine::Vector3& endEuler, float rate) {
    const engine::Quaternion start = engine::Math::EulerToQuaternion(startEuler);
    const engine::Quaternion end = engine::Math::EulerToQuaternion(endEuler);
    const engine::Quaternion result = engine::Math::Slerp(start, end, Clamp01(rate));
    return engine::Math::MatrixToEuler(engine::Math::MakeRotateQuaternionMatrix(result));
}

/// <summary>Euler 角を Quaternion 経由で追従させます。</summary>
inline engine::Vector3 DampEuler(const engine::Vector3& currentEuler, const engine::Vector3& targetEuler, float response, float deltaTime) {
    return SlerpEuler(currentEuler, targetEuler, FollowAlpha(response, deltaTime));
}

/// <summary>指定時間区間内での進行割合を 0～1 で返します。</summary>
/// <remarks>区間長が 0 に近いときは、終端へ到達したかどうかで 0 または 1 を返します。</remarks>
inline float SegmentRate(float time, float startTime, float endTime) {
    const float duration = endTime - startTime;
    if (duration <= 0.0001f) {
        return time >= endTime ? 1.0f : 0.0f;
    }
    return Clamp01((time - startTime) / duration);
}

} // namespace AnimationInterpolation

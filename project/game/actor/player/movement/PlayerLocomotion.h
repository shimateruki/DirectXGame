#pragma once
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cmath>

// 最高速度はPlayerの移動速度を使い、到達・停止に掛ける秒数だけをここで調整する。
struct PlayerLocomotionSettings {
    float accelerationTime = 0.4f;
    float brakingTime = 0.1f;
    float airControl = 0.65f;

    void Normalize() {
        accelerationTime = std::isfinite(accelerationTime) ? std::clamp(accelerationTime, 0.0f, 3.0f) : 0.4f;
        brakingTime = std::isfinite(brakingTime) ? std::clamp(brakingTime, 0.0f, 3.0f) : 0.1f;
        airControl = std::isfinite(airControl) ? std::clamp(airControl, 0.1f, 1.0f) : 0.65f;
    }

    engine::Vector3 Apply(const engine::Vector3& current, const engine::Vector3& desired,
                         float maximumSpeed, bool grounded, float dt) const {
        auto result = current;
        if (!std::isfinite(dt) || dt <= 0.0f || !std::isfinite(maximumSpeed))
            return result;
        if (maximumSpeed <= 0.0f) {
            result.x = result.z = 0.0f;
            return result;
        }
        auto settings = *this;
        settings.Normalize();
        float targetX = desired.x, targetZ = desired.z;
        const float desiredSpeed = std::hypot(targetX, targetZ);
        if (desiredSpeed > maximumSpeed) {
            const float limit = maximumSpeed / desiredSpeed;
            targetX *= limit; targetZ *= limit;
        }
        const float response = grounded ? 1.0f : settings.airControl;
        float remaining = dt;
        float speed = std::hypot(result.x, result.z);

        // 逆入力では一度止める。停止後の余り時間も加速へ使い、FPSで切り返し時間を変えない。
        if (speed > 0.0001f && result.x * targetX + result.z * targetZ < 0.0f) {
            const float stopTime = settings.brakingTime * speed / (maximumSpeed * response);
            if (stopTime > remaining) {
                const float keep = 1.0f - remaining / stopTime;
                result.x *= keep; result.z *= keep;
                return result;
            }
            remaining -= stopTime;
            result.x = result.z = 0.0f;
            speed = 0.0f;
        }
        const float targetSpeed = std::hypot(targetX, targetZ);
        const float seconds = targetSpeed < speed ? settings.brakingTime : settings.accelerationTime;
        const float dx = targetX - result.x, dz = targetZ - result.z;
        const float distance = std::hypot(dx, dz);
        if (distance <= 0.0001f || seconds <= 0.0f) {
            result.x = targetX; result.z = targetZ;
            return result;
        }
        // ベクトル全体の変化量を制限し、斜め入力や方向変更でも最高速度を超えさせない。
        const float blend = (std::min)(1.0f, maximumSpeed * response * remaining / (seconds * distance));
        result.x += dx * blend;
        result.z += dz * blend;
        return result;
    }
};
#pragma once

#include "engine/utility/math/Math.h"

#include <algorithm>
#include <cmath>

// スライム系の通常移動に使う、共通のぷにぷに伸縮。
namespace SlimeBounceAnimator {

struct Params {
    float speedForFullBounce = 2.5f;
    float idleAmplitude = 0.05f;
    float moveAmplitude = 0.15f;
    float hopFrequency = 8.5f;
    float horizontalSquash = 0.17f;
    float verticalStretch = 0.22f;
    float airborneStretch = 0.24f;
};

inline float PlanarSpeed(const Vector3& velocity) {
    return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}

inline bool StepGroundHop(float& hopTimer, const Vector3& velocity, bool grounded, float deltaTime, float interval, float speedThreshold) {
    if (!grounded) {
        return false;
    }

    if (PlanarSpeed(velocity) <= speedThreshold) {
        hopTimer = interval * 0.72f;
        return false;
    }

    hopTimer += deltaTime;
    if (hopTimer < interval) {
        return false;
    }

    hopTimer = 0.0f;
    return true;
}

inline Vector3 MakeScale(const Vector3& baseScale, const Vector3& velocity, float timer, bool grounded, const Params& params = {}) {
    const float speedRate = std::clamp(PlanarSpeed(velocity) / (std::max)(params.speedForFullBounce, 0.01f), 0.0f, 1.0f);
    const float idlePulse = std::sin(timer * 3.1f) * params.idleAmplitude;

    Vector3 scale = baseScale;
    if (!grounded) {
        const float fallRate = std::clamp(std::abs(velocity.y) / 22.0f, 0.0f, 1.0f);
        const float stretch = params.airborneStretch * (0.45f + fallRate * 0.55f);
        scale.x = baseScale.x * (1.0f - stretch * 0.42f);
        scale.y = baseScale.y * (1.0f + stretch);
        scale.z = baseScale.z * (1.0f - stretch * 0.42f);
        return scale;
    }

    const float phase = timer * (params.hopFrequency + speedRate * 4.0f);
    const float wave = std::sin(phase);
    const float squash = (std::max)(0.0f, -wave) * speedRate;
    const float stretch = (std::max)(0.0f, wave) * speedRate;
    const float sideWobble = std::sin(phase * 0.5f + 0.8f) * params.moveAmplitude * 0.22f * speedRate;
    const float movingBody = params.moveAmplitude * speedRate;

    scale.x = baseScale.x * (1.0f + idlePulse * 0.55f + movingBody * 0.35f + squash * params.horizontalSquash - stretch * 0.06f + sideWobble);
    scale.y = baseScale.y * (1.0f - idlePulse * 0.85f - movingBody * 0.10f - squash * params.horizontalSquash * 0.92f + stretch * params.verticalStretch);
    scale.z = baseScale.z * (1.0f + idlePulse * 0.55f + movingBody * 0.28f + squash * params.horizontalSquash * 0.78f - stretch * 0.05f - sideWobble * 0.7f);
    return scale;
}

inline Vector3 MakeChargeSquash(const Vector3& baseScale, float chargeRate, float timer, float strength = 1.0f) {
    chargeRate = std::clamp(chargeRate, 0.0f, 1.0f);
    const float pulse = std::sin(timer * 18.0f) * 0.04f * chargeRate * strength;
    return {
        baseScale.x * (1.0f + chargeRate * 0.34f * strength + pulse),
        baseScale.y * (1.0f - chargeRate * 0.34f * strength),
        baseScale.z * (1.0f + chargeRate * 0.34f * strength - pulse)
    };
}

} // namespace SlimeBounceAnimator

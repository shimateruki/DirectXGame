#include "EnemySlime.h"
#include "SlimeBounceAnimator.h"
#include "DebrisEffectManager.h"
#include "HitEffectDirector.h"
#include "MeshEffectManager.h"
#include "engine/utility/math/Math.h"

#include <PlayerState.h>
#include "Player.h"
#include <DebugConsole.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kChargePulseEffectPath = "Resources/json/effect/effect_pink_slime_charge_pulse_ring.json";
constexpr const char* kChargeCoreEffectPath = "Resources/json/effect/effect_pink_slime_charge_core_flash.json";
constexpr const char* kChargeVortexEffectPath = "Resources/json/effect/effect_pink_slime_charge_vortex_streak.json";
constexpr const char* kLaunchRingEffectPath = "Resources/json/effect/effect_pink_slime_launch_kick_ring.json";
constexpr const char* kApexFlashEffectPath = "Resources/json/effect/effect_pink_slime_apex_focus_flash.json";
constexpr const char* kDiveTrailEffectPath = "Resources/json/effect/effect_pink_slime_dive_streak.json";
constexpr const char* kLandingRingEffectPath = "Resources/json/effect/effect_pink_slime_landing_burst_ring.json";
constexpr const char* kLandingCoreEffectPath = "Resources/json/effect/effect_pink_slime_landing_core_flash.json";
constexpr const char* kLandingShockArcEffectPath = "Resources/json/effect/effect_pink_slime_landing_shock_arc.json";
constexpr const char* kChargeDebrisPresetName = "pink_slime_charge_pebble_pull";
constexpr const char* kLandingDebrisPresetName = "pink_slime_landing_pebble_burst";
constexpr float kSlimeModelYawOffset = 3.1415926535f;
constexpr float kMinDiveDistance = 0.35f;
constexpr float kChargeDuration = 1.35f;
constexpr float kChargePulseInterval = 0.56f;
constexpr float kChargeDebrisInterval = 0.64f;
constexpr float kChargeVortexInterval = 0.34f;
constexpr float kDiveFallbackDetectionRange = 12.0f;
constexpr float kChargeTelegraphRadius = 1.35f;
constexpr float kRiseJumpSpeed = 32.0f;
constexpr float kRiseApexMinTime = 0.16f;
constexpr float kRiseMaxDuration = 1.15f;
constexpr float kDiveMinSpeed = 22.0f;
constexpr float kDiveMaxSpeed = 40.0f;
constexpr float kDiveDistanceSpeedRate = 0.72f;
constexpr float kDiveSteerStrength = 5.5f;
constexpr float kDiveStartDownSpeed = -16.0f;
constexpr float kDiveDropSpeed = -62.0f;
constexpr float kDiveMaxDuration = 0.46f;
constexpr float kDiveTrailInterval = 0.12f;
constexpr float kWanderHopInterval = 1.15f;
constexpr float kLandingRecoverDuration = 0.24f;
constexpr float kLandingSquashDuration = 0.20f;
constexpr float kCarriedChargeMinDuration = 0.45f;
constexpr float kCarriedChargeMaxDuration = 1.05f;
constexpr float kCarriedRiseJumpSpeed = 34.0f;
constexpr float kCarriedRiseApexMinTime = 0.14f;
constexpr float kCarriedRiseMaxDuration = 0.92f;
constexpr float kCarriedDiveSpeed = 38.0f;
constexpr float kCarriedDiveStartDownSpeed = -18.0f;
constexpr float kCarriedDiveDropSpeed = -74.0f;
constexpr float kCarriedDiveMaxDuration = 0.42f;
constexpr float kCarriedRecoverDuration = 0.18f;
constexpr float kCarriedDiveTrailInterval = 0.085f;
constexpr float kCarriedThrustSpeed = 58.0f;
constexpr float kCarriedThrustDuration = 0.24f;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SmoothStep01(float value) {
    const float t = Clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

float PlanarLength(const Vector3& value) {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

Vector3 NormalizePlanarOr(const Vector3& value, const Vector3& fallback) {
    const float length = PlanarLength(value);
    if (length <= 0.0001f) {
        return fallback;
    }
    return { value.x / length, 0.0f, value.z / length };
}

float LerpFloat(float from, float to, float rate) {
    return from + (to - from) * Clamp01(rate);
}

Vector3 MakeDiveEffectRotation(const Vector3& direction, float pitch) {
    return { pitch, std::atan2(direction.x, direction.z), 0.0f };
}

void SpawnMeshEffectAtSafe(const char* path, const Vector3& position, const Vector3& rotation, const Vector3& scale) {
    if (auto* meshEffect = MeshEffectManager::GetInstance()) {
        meshEffect->SpawnEffectAt(path, position, rotation, scale);
    }
}

Vector3 ResolveSlimeGroundEffectPosition(const Vector3& position, float lift = 0.04f) {
    Vector3 groundPosition = HitEffectDirector::ResolveGroundEffectPosition(position);
    groundPosition.y += lift;
    return groundPosition;
}

void SpawnDebrisOnGroundSafe(const char* presetName, const Vector3& position, float lift = 0.04f) {
    const Vector3 groundPosition = HitEffectDirector::ResolveGroundEffectPosition(position);
    Vector3 effectPosition = groundPosition;
    effectPosition.y += lift;
    if (auto* debrisEffect = DebrisEffectManager::GetInstance()) {
        debrisEffect->SpawnOnGround(presetName, effectPosition, groundPosition.y);
    }
}

}

void EnemySlime::Update(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (isCarried_) {
        HideAttackTelegraph();
        return;
    }
    if (IsThrowRecovering()) {
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (!target_ || !param_.has_value()) {
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return;
    }

    EnsureBaseScale();
    idleTimer_ += deltaTime;

    float targetDistance = 0.0f;
    Vector3 targetDirection = GetTargetPlanarDirection(&targetDistance);
    float effectiveDetectionRange = detectionRange_;
    if (param_.has_value()) {
        effectiveDetectionRange = (std::max)(effectiveDetectionRange, param_->detectionRange);
    }
    effectiveDetectionRange = (std::max)(effectiveDetectionRange, kDiveFallbackDetectionRange);
    const bool canDiveAtTarget = targetDistance <= effectiveDetectionRange && targetDistance >= kMinDiveDistance;

    if (moveState_ == MoveState::Dive && isGrounded_ && diveTimer_ > 0.18f && velocity_.y <= 0.0f) {
        BeginLandingRecovery();
    }

    if (moveState_ == MoveState::Recover) {
        UpdateLandingRecovery(deltaTime);
    } else if (moveState_ == MoveState::Rise) {
        UpdateRise(deltaTime);
    } else if (moveState_ == MoveState::Dive) {
        UpdateDive(deltaTime);
    } else if (isGrounded_) {
        if (canDiveAtTarget) {
            UpdateCharge(deltaTime, targetDirection, targetDistance);
        } else {
            UpdateWander(deltaTime);
        }
    } else {
        HideAttackTelegraph();
    }

    ApplySlimeAnimation(deltaTime);
    BaseEnemy::Update(deltaTime);
}

void EnemySlime::EnsureBaseScale() {
    if (hasBaseScale_) {
        return;
    }

    baseScale_ = GetScale();
    const float maxScale = (std::max)({ std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z) });
    if (maxScale < 1.2f) {
        baseScale_ = { 2.0f, 2.0f, 2.0f };
        SetScale(baseScale_);
    }
    hasBaseScale_ = true;
}

Vector3 EnemySlime::GetTargetPlanarDirection(float* outDistance) const {
    if (!target_) {
        if (outDistance) {
            *outDistance = 0.0f;
        }
        return GetFacingPlanarDirection();
    }

    Vector3 myPos = transform_.translate;
    Vector3 targetPos = target_->GetWorldPosition();
    targetPos.y = myPos.y;
    Vector3 toTarget = targetPos - myPos;
    toTarget.y = 0.0f;

    const float distance = PlanarLength(toTarget);
    if (outDistance) {
        *outDistance = distance;
    }
    return NormalizePlanarOr(toTarget, GetFacingPlanarDirection());
}

Vector3 EnemySlime::GetFacingPlanarDirection() const {
    const float yaw = GetRotation().y - kSlimeModelYawOffset;
    return { std::sin(yaw), 0.0f, std::cos(yaw) };
}

void EnemySlime::FaceDirection(const Vector3& direction, float turnRate) {
    const float length = PlanarLength(direction);
    if (length <= 0.0001f) {
        return;
    }

    static Math math;
    const float targetYaw = std::atan2(direction.x, direction.z) + kSlimeModelYawOffset;
    SetRotationY(math.LerpShortAngle(GetRotation().y, targetYaw, Clamp01(turnRate)));
}

void EnemySlime::UpdateWander(float deltaTime) {
    HideAttackTelegraph();
    moveState_ = MoveState::Wander;
    chargeTimer_ = 0.0f;
    riseTimer_ = 0.0f;
    diveTimer_ = 0.0f;
    chargeEffectTimer_ = 0.0f;
    chargeDebrisTimer_ = 0.0f;
    chargeVortexTimer_ = 0.0f;
    diveTrailTimer_ = 0.0f;

    const float wanderSpeed = (std::max)(1.35f, param_->speed * 3.0f);
    Vector3 wanderVelocity = CalculateWanderVelocity(deltaTime, wanderSpeed, 0.65f);
    Vector3 wanderDirection = NormalizePlanarOr(wanderVelocity, GetFacingPlanarDirection());
    const float wanderPlanarSpeed = PlanarLength(wanderVelocity);

    if (wanderPlanarSpeed > 0.05f) {
        FaceDirection(wanderDirection, 0.08f);
    }

    jumpTimer_ += deltaTime;
    if (jumpTimer_ >= kWanderHopInterval && wanderPlanarSpeed > 0.05f) {
        const float jumpPower = param_->jumpPower > 0.0f ? param_->jumpPower * 0.46f : 7.5f;
        velocity_.x = wanderDirection.x * wanderSpeed;
        velocity_.z = wanderDirection.z * wanderSpeed;
        velocity_.y = jumpPower;
        jumpTimer_ = 0.0f;
        return;
    }

    velocity_.x *= 0.82f;
    velocity_.z *= 0.82f;
}

void EnemySlime::UpdateCharge(float deltaTime, const Vector3& direction, float distance) {
    (void)distance;
    moveState_ = MoveState::Charge;
    jumpTimer_ = 0.0f;
    diveDirection_ = NormalizePlanarOr(direction, diveDirection_);

    const float chargeRate = Clamp01(chargeTimer_ / kChargeDuration);
    const float ringPulse = std::sin(idleTimer_ * 14.0f) * 0.06f * (0.35f + chargeRate);
    const Vector3 telegraphCenter = ResolveSlimeGroundEffectPosition(GetTranslate(), 0.05f);
    ShowAttackTelegraphCircle(
        telegraphCenter,
        kChargeTelegraphRadius + chargeRate * 0.24f + ringPulse,
        (std::max)(0.20f, chargeRate),
        { 1.0f, 0.24f, 0.76f, 0.84f });
    SpawnChargePulseEffect(deltaTime, chargeRate);
    SpawnChargeDebrisEffect(deltaTime, chargeRate);
    SpawnChargeVortexEffect(deltaTime, chargeRate);
    FaceDirection(diveDirection_, 0.11f + chargeRate * 0.10f);

    velocity_.x = 0.0f;
    velocity_.z = 0.0f;
    chargeTimer_ += deltaTime;

    if (chargeTimer_ >= kChargeDuration) {
        BeginRise(diveDirection_);
    }
}

void EnemySlime::BeginRise(const Vector3& direction) {
    moveState_ = MoveState::Rise;
    chargeTimer_ = 0.0f;
    riseTimer_ = 0.0f;
    diveTimer_ = 0.0f;
    chargeEffectTimer_ = 0.0f;
    chargeDebrisTimer_ = 0.0f;
    chargeVortexTimer_ = 0.0f;
    diveTrailTimer_ = 0.0f;
    landingSquashTimer_ = 0.0f;
    diveDirection_ = NormalizePlanarOr(direction, GetFacingPlanarDirection());

    HideAttackTelegraph();
    SpawnLaunchEffect();

    isGrounded_ = false;
    velocity_.x = 0.0f;
    velocity_.z = 0.0f;
    velocity_.y = (std::max)(kRiseJumpSpeed, param_.has_value() ? param_->jumpPower * 1.55f : 0.0f);
    FaceDirection(diveDirection_, 1.0f);
}

void EnemySlime::UpdateRise(float deltaTime) {
    HideAttackTelegraph();
    riseTimer_ += deltaTime;

    float distance = 0.0f;
    Vector3 targetDirection = GetTargetPlanarDirection(&distance);
    diveDirection_ = NormalizePlanarOr(targetDirection, diveDirection_);

    FaceDirection(diveDirection_, 0.16f);
    velocity_.x *= 0.58f;
    velocity_.z *= 0.58f;

    const bool reachedApex = riseTimer_ >= kRiseApexMinTime && velocity_.y <= 0.0f;
    const bool forcedDive = riseTimer_ >= kRiseMaxDuration;
    if (reachedApex || forcedDive) {
        SpawnApexEffect();
        BeginDive(diveDirection_, distance);
    }
}

void EnemySlime::BeginDive(const Vector3& direction, float distance) {
    moveState_ = MoveState::Dive;
    chargeTimer_ = 0.0f;
    diveTimer_ = 0.0f;
    chargeEffectTimer_ = 0.0f;
    chargeDebrisTimer_ = 0.0f;
    chargeVortexTimer_ = 0.0f;
    diveTrailTimer_ = 0.0f;
    landingSquashTimer_ = 0.0f;
    HideAttackTelegraph();
    diveDirection_ = NormalizePlanarOr(direction, GetFacingPlanarDirection());

    const float speedFromDistance = distance * kDiveDistanceSpeedRate;
    const float speedFromStatus = param_.has_value() ? param_->speed * 2.0f : 0.0f;
    diveSpeed_ = std::clamp(kDiveMinSpeed + speedFromDistance + speedFromStatus, kDiveMinSpeed, kDiveMaxSpeed);

    velocity_.x = diveDirection_.x * diveSpeed_;
    velocity_.z = diveDirection_.z * diveSpeed_;
    velocity_.y = kDiveStartDownSpeed;
    FaceDirection(diveDirection_, 1.0f);
}

void EnemySlime::UpdateDive(float deltaTime) {
    HideAttackTelegraph();
    diveTimer_ += deltaTime;
    SpawnDiveTrailEffect(deltaTime);

    float distance = 0.0f;
    Vector3 targetDirection = GetTargetPlanarDirection(&distance);
    if (distance > 0.65f) {
        const float steerRate = deltaTime * kDiveSteerStrength;
        Vector3 blended = {
            LerpFloat(diveDirection_.x, targetDirection.x, steerRate),
            0.0f,
            LerpFloat(diveDirection_.z, targetDirection.z, steerRate)
        };
        diveDirection_ = NormalizePlanarOr(blended, diveDirection_);
        FaceDirection(diveDirection_, 0.22f);
    }

    const float curveRate = SmoothStep01(diveTimer_ / kDiveMaxDuration);
    const float targetSpeed = diveSpeed_ * (1.0f + curveRate * 0.34f);
    const float velocityRate = deltaTime * (kDiveSteerStrength + curveRate * 5.5f);
    velocity_.x = LerpFloat(velocity_.x, diveDirection_.x * targetSpeed, velocityRate);
    velocity_.z = LerpFloat(velocity_.z, diveDirection_.z * targetSpeed, velocityRate);

    const float diveRate = SmoothStep01(diveTimer_ / kDiveMaxDuration);
    const float targetYSpeed = LerpFloat(kDiveStartDownSpeed, kDiveDropSpeed, diveRate);
    velocity_.y = LerpFloat(velocity_.y, targetYSpeed, deltaTime * 22.0f);

    if (diveTimer_ > kDiveMaxDuration) {
        diveTimer_ = kDiveMaxDuration;
    }
}

void EnemySlime::BeginLandingRecovery() {
    moveState_ = MoveState::Recover;
    recoverTimer_ = kLandingRecoverDuration;
    landingSquashTimer_ = kLandingSquashDuration;
    chargeTimer_ = 0.0f;
    riseTimer_ = 0.0f;
    diveTimer_ = 0.0f;
    chargeEffectTimer_ = 0.0f;
    chargeDebrisTimer_ = 0.0f;
    chargeVortexTimer_ = 0.0f;
    diveTrailTimer_ = 0.0f;
    SpawnLandingEffect();
    velocity_.x *= 0.34f;
    velocity_.z *= 0.34f;
    if (velocity_.y < 0.0f) {
        velocity_.y = 0.0f;
    }
}

void EnemySlime::UpdateLandingRecovery(float deltaTime) {
    HideAttackTelegraph();
    recoverTimer_ = (std::max)(0.0f, recoverTimer_ - deltaTime);
    landingSquashTimer_ = (std::max)(0.0f, landingSquashTimer_ - deltaTime);
    velocity_.x *= 0.62f;
    velocity_.z *= 0.62f;

    if (recoverTimer_ <= 0.0f) {
        moveState_ = MoveState::Wander;
    }
}

void EnemySlime::SpawnChargePulseEffect(float deltaTime, float chargeRate) {
    if (chargeRate < 0.26f) {
        return;
    }
    chargeEffectTimer_ += deltaTime;
    const float interval = (std::max)(0.34f, kChargePulseInterval - chargeRate * 0.12f);
    if (chargeEffectTimer_ < interval) {
        return;
    }
    chargeEffectTimer_ = 0.0f;

    Vector3 position = ResolveSlimeGroundEffectPosition(GetTranslate(), 0.06f);
    const float scale = 0.84f + chargeRate * 0.82f;
    SpawnMeshEffectAtSafe(kChargePulseEffectPath, position, { 0.0f, 0.0f, 0.0f }, { scale, 0.9f, scale });

    if (chargeRate > 0.52f) {
        Vector3 corePosition = GetTranslate();
        corePosition.y += (std::max)(0.45f, GetScale().y * 0.28f);
        const float coreScale = 0.34f + chargeRate * 0.42f;
        SpawnMeshEffectAtSafe(kChargeCoreEffectPath, corePosition, MakeDiveEffectRotation(diveDirection_, -0.10f), { coreScale, coreScale, coreScale });
    }
}

void EnemySlime::SpawnChargeDebrisEffect(float deltaTime, float chargeRate) {
    if (chargeRate < 0.18f) {
        return;
    }
    chargeDebrisTimer_ += deltaTime;
    const float interval = (std::max)(0.38f, kChargeDebrisInterval - chargeRate * 0.12f);
    if (chargeDebrisTimer_ < interval) {
        return;
    }
    chargeDebrisTimer_ = 0.0f;

    SpawnDebrisOnGroundSafe(kChargeDebrisPresetName, GetTranslate(), 0.04f);
}

void EnemySlime::SpawnChargeVortexEffect(float deltaTime, float chargeRate) {
    if (chargeRate < 0.42f) {
        return;
    }
    chargeVortexTimer_ += deltaTime;
    const float interval = (std::max)(0.22f, kChargeVortexInterval - chargeRate * 0.05f);
    if (chargeVortexTimer_ < interval) {
        return;
    }
    chargeVortexTimer_ = 0.0f;

    const Vector3 center = ResolveSlimeGroundEffectPosition(GetTranslate(), 0.30f + chargeRate * 0.24f);
    const float baseAngle = idleTimer_ * (7.4f + chargeRate * 3.2f);
    const float radius = (std::max)(0.48f, 1.20f - chargeRate * 0.34f + std::sin(idleTimer_ * 13.0f) * 0.08f);
    const float scale = 0.54f + chargeRate * 0.30f;

    Vector3 position = center;
    position.x += std::cos(baseAngle) * radius;
    position.z += std::sin(baseAngle) * radius;
    Vector3 toCenter = {
        center.x - position.x,
        0.0f,
        center.z - position.z
    };
    SpawnMeshEffectAtSafe(kChargeVortexEffectPath, position, MakeDiveEffectRotation(toCenter, -0.18f), { scale, scale, 1.12f + chargeRate * 0.46f });
}

void EnemySlime::SpawnLaunchEffect() {
    Vector3 position = ResolveSlimeGroundEffectPosition(GetTranslate(), 0.08f);
    SpawnMeshEffectAtSafe(kLaunchRingEffectPath, position, { 0.0f, 0.0f, 0.0f }, { 1.15f, 0.9f, 1.15f });
}

void EnemySlime::SpawnApexEffect() {
    Vector3 position = GetTranslate();
    position.y += 0.28f;
    SpawnMeshEffectAtSafe(kApexFlashEffectPath, position, MakeDiveEffectRotation(diveDirection_, -0.18f), { 0.82f, 0.82f, 0.82f });
}

void EnemySlime::SpawnDiveTrailEffect(float deltaTime) {
    diveTrailTimer_ -= deltaTime;
    if (diveTrailTimer_ > 0.0f) {
        return;
    }
    diveTrailTimer_ = kDiveTrailInterval;

    Vector3 position = GetTranslate();
    position.x -= diveDirection_.x * 0.52f;
    position.y += 0.22f;
    position.z -= diveDirection_.z * 0.52f;
    const float diveRate = SmoothStep01(diveTimer_ / kDiveMaxDuration);
    const float scaleZ = 1.35f + diveRate * 0.80f;
    SpawnMeshEffectAtSafe(kDiveTrailEffectPath, position, MakeDiveEffectRotation(diveDirection_, -0.52f), { 0.58f, 0.58f, scaleZ });
}

void EnemySlime::SpawnLandingEffect() {
    Vector3 position = ResolveSlimeGroundEffectPosition(GetTranslate(), 0.06f);
    SpawnDebrisOnGroundSafe(kLandingDebrisPresetName, GetTranslate(), 0.05f);
    SpawnMeshEffectAtSafe(kLandingRingEffectPath, position, { 0.0f, 0.0f, 0.0f }, { 1.85f, 0.95f, 1.85f });
    Vector3 flashPosition = position;
    flashPosition.y += 0.34f;
    SpawnMeshEffectAtSafe(kLandingCoreEffectPath, flashPosition, MakeDiveEffectRotation(diveDirection_, -0.42f), { 1.10f, 1.10f, 1.10f });
    SpawnMeshEffectAtSafe(kLandingShockArcEffectPath, position, MakeDiveEffectRotation(diveDirection_, -0.04f), { 1.42f, 1.42f, 1.42f });
    SpawnMeshEffectAtSafe(kLandingShockArcEffectPath, position, MakeDiveEffectRotation({ -diveDirection_.z, 0.0f, diveDirection_.x }, -0.04f), { 1.16f, 1.16f, 1.16f });
}

void EnemySlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    SlimeBounceAnimator::Params bounceParams;
    bounceParams.speedForFullBounce = 2.2f;
    bounceParams.idleAmplitude = 0.09f;
    bounceParams.moveAmplitude = 0.34f;
    bounceParams.hopFrequency = 11.2f;
    bounceParams.horizontalSquash = 0.34f;
    bounceParams.verticalStretch = 0.40f;
    bounceParams.airborneStretch = 0.42f;

    if (moveState_ == MoveState::Charge) {
        const float chargeRate = Clamp01(chargeTimer_ / kChargeDuration);
        targetScale = SlimeBounceAnimator::MakeChargeSquash(baseScale_, chargeRate, idleTimer_, 2.2f);
        const float tremble = std::sin(idleTimer_ * 34.0f) * 0.045f * chargeRate;
        targetScale.x *= 1.0f + tremble;
        targetScale.y *= 1.0f - chargeRate * 0.10f;
        targetScale.z *= 1.0f - tremble * 0.75f;
    } else if (moveState_ == MoveState::Rise) {
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, velocity_, idleTimer_, false, bounceParams);
        const float risePose = Clamp01(riseTimer_ / 0.20f);
        targetScale.x *= 1.0f - risePose * 0.20f;
        targetScale.y *= 1.0f + risePose * 0.42f;
        targetScale.z *= 1.0f - risePose * 0.14f;
    } else if (moveState_ == MoveState::Recover && landingSquashTimer_ > 0.0f) {
        const float squashRate = SmoothStep01(landingSquashTimer_ / kLandingSquashDuration);
        targetScale = {
            baseScale_.x * (1.0f + squashRate * 0.34f),
            baseScale_.y * (1.0f - squashRate * 0.30f),
            baseScale_.z * (1.0f + squashRate * 0.26f)
        };
    } else if (!isGrounded_) {
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, velocity_, idleTimer_, false, bounceParams);
        if (moveState_ == MoveState::Dive) {
            const float divePose = Clamp01(diveTimer_ / 0.24f);
            targetScale.x *= 1.0f - divePose * 0.14f;
            targetScale.y *= 1.0f + divePose * 0.18f;
            targetScale.z *= 1.0f + divePose * 0.34f;
        }
    } else {
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, velocity_, idleTimer_, true, bounceParams);
    }

    SetScale(Math::Lerp(GetScale(), targetScale, (std::min)(1.0f, deltaTime * 16.0f)));

    Vector3 currentRotation = GetRotation();
    Vector3 targetRotation = { 0.0f, currentRotation.y, 0.0f };
    if (moveState_ == MoveState::Charge) {
        const float chargeRate = Clamp01(chargeTimer_ / kChargeDuration);
        const float wobble = std::sin(idleTimer_ * 30.0f) * chargeRate * 0.09f;
        targetRotation.x = -diveDirection_.z * wobble;
        targetRotation.z = diveDirection_.x * wobble;
    } else if (moveState_ == MoveState::Rise) {
        const float risePose = Clamp01(riseTimer_ / 0.24f);
        targetRotation.x = -diveDirection_.z * 0.18f * risePose;
        targetRotation.z = diveDirection_.x * 0.18f * risePose;
    } else if (moveState_ == MoveState::Dive) {
        const float divePose = SmoothStep01(diveTimer_ / kDiveMaxDuration);
        const float flutter = std::sin(diveTimer_ * 48.0f) * 0.045f;
        const float lean = 0.28f + divePose * 0.18f + flutter;
        targetRotation.x = -diveDirection_.z * lean;
        targetRotation.z = diveDirection_.x * lean;
    } else if (moveState_ == MoveState::Recover) {
        const float recoverRate = recoverTimer_ / (std::max)(kLandingRecoverDuration, 0.01f);
        const float shake = std::sin(idleTimer_ * 38.0f) * recoverRate * 0.10f;
        targetRotation.x = -diveDirection_.z * shake;
        targetRotation.z = diveDirection_.x * shake;
    }

    const float rotationRate = (std::min)(1.0f, deltaTime * 17.0f);
    SetRotation({
        LerpFloat(currentRotation.x, targetRotation.x, rotationRate),
        currentRotation.y,
        LerpFloat(currentRotation.z, targetRotation.z, rotationRate)
    });
}

std::unique_ptr<Object3d> EnemySlime::Clone() const {
    auto newSlime = std::make_unique<EnemySlime>();
    newSlime->Initialize(common_, this->GetModelName());
    newSlime->CopyFrom(this);
    newSlime->SetTarget(this->target_);
    newSlime->SetDetectionRange(this->detectionRange_);
    return newSlime;
}

void EnemySlime::ExecuteAbility(Player* player) {
    BeginCarriedThrust(player);
}

void EnemySlime::UpdateCarriedAbility(Player* player, float deltaTime) {
    if (!player || deltaTime <= 0.0f) {
        return;
    }
    if (!player->IsPinkSlimeMorphed()) {
        ResetCarriedAbility(player, false);
        return;
    }

    idleTimer_ += deltaTime;

    switch (carriedAbilityState_) {
    case CarriedAbilityState::Charge:
        UpdateCarriedCharge(player, deltaTime);
        break;
    case CarriedAbilityState::Rise:
        UpdateCarriedRise(player, deltaTime);
        break;
    case CarriedAbilityState::Dive:
        UpdateCarriedDive(player, deltaTime);
        break;
    case CarriedAbilityState::Thrust:
        UpdateCarriedThrust(player, deltaTime);
        break;
    case CarriedAbilityState::Recover:
        carriedRecoverTimer_ = (std::max)(0.0f, carriedRecoverTimer_ - deltaTime);
        if (carriedRecoverTimer_ <= 0.0f) {
            ResetCarriedAbility(player, true);
        }
        break;
    case CarriedAbilityState::Idle:
    default:
        if (InputManager* input = player->GetInputManager()) {
            if (!player->IsGrounded() && input->IsMouseButtonTriggered(0)) {
                carriedDiveDirection_ = GetPlayerDiveDirection(player);
                BeginCarriedDive(player);
            }
        }
        break;
    }
}

void EnemySlime::CancelCarriedAbility(Player* player) {
    ResetCarriedAbility(player, true);
}

void EnemySlime::BeginCarriedCharge(Player* player) {
    if (!player || carriedAbilityState_ != CarriedAbilityState::Idle) {
        return;
    }

    carriedAbilityState_ = CarriedAbilityState::Charge;
    carriedChargeTimer_ = 0.0f;
    carriedRiseTimer_ = 0.0f;
    carriedDiveTimer_ = 0.0f;
    carriedRecoverTimer_ = 0.0f;
    carriedChargeEffectTimer_ = 0.0f;
    carriedDiveTrailTimer_ = 0.0f;
    carriedDiveDirection_ = GetPlayerDiveDirection(player);

    player->SetIsControlActive(false);
    player->SetVelocity({ 0.0f, 0.0f, 0.0f });
    player->SetMoveYaw(std::atan2(carriedDiveDirection_.x, carriedDiveDirection_.z));
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);
    player->SetSlimeJumpCharge(0.0f);
    player->TriggerSlimeImpulse({ 2.75f, 0.68f, 2.75f }, 0.18f);
    SpawnCarriedChargeEffect(player, 999.0f, 0.45f);

    DebugConsole::GetInstance()->AddLog("Ability Activated: Pink Slime Pounce Charge!");
}

void EnemySlime::UpdateCarriedCharge(Player* player, float deltaTime) {
    if (!player) {
        return;
    }

    InputManager* input = player->GetInputManager();
    const bool abilityHeld = input && input->IsActionPressed("Jump");

    carriedDiveDirection_ = GetPlayerDiveDirection(player);
    carriedChargeTimer_ = (std::min)(kCarriedChargeMaxDuration, carriedChargeTimer_ + deltaTime);
    const float chargeRate = Clamp01(carriedChargeTimer_ / kCarriedChargeMaxDuration);

    player->SetIsControlActive(false);
    player->SetVelocity({ 0.0f, 0.0f, 0.0f });
    player->SetMoveYaw(std::atan2(carriedDiveDirection_.x, carriedDiveDirection_.z));
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);
    player->SetSlimeJumpCharge(chargeRate);
    SpawnCarriedChargeEffect(player, deltaTime, chargeRate);

    const bool chargeReady = carriedChargeTimer_ >= kCarriedChargeMinDuration;
    if ((chargeReady && !abilityHeld) || carriedChargeTimer_ >= kCarriedChargeMaxDuration) {
        BeginCarriedRise(player);
    }
}

void EnemySlime::BeginCarriedRise(Player* player) {
    if (!player) {
        return;
    }

    carriedAbilityState_ = CarriedAbilityState::Rise;
    carriedChargeTimer_ = 0.0f;
    carriedRiseTimer_ = 0.0f;
    carriedDiveTimer_ = 0.0f;
    carriedChargeEffectTimer_ = 0.0f;
    carriedDiveTrailTimer_ = 0.0f;
    carriedDiveDirection_ = GetPlayerDiveDirection(player);

    player->SetIsControlActive(false);
    player->SetSlimeJumpCharge(0.0f);
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);
    player->ChangeState(std::make_unique<PlayerStateJump>());
    player->SetMoveYaw(std::atan2(carriedDiveDirection_.x, carriedDiveDirection_.z));
    player->SetVelocity({ 0.0f, kCarriedRiseJumpSpeed, 0.0f });
    player->SetIsControlActive(true);
    player->TriggerSlimeImpulse({ 1.36f, 3.15f, 1.36f }, 0.18f);

    Vector3 effectPos = ResolveSlimeGroundEffectPosition(player->GetWorldPosition(), 0.08f);
    SpawnMeshEffectAtSafe(kLaunchRingEffectPath, effectPos, { 0.0f, 0.0f, 0.0f }, { 1.25f, 0.9f, 1.25f });
}

void EnemySlime::UpdateCarriedRise(Player* player, float deltaTime) {
    if (!player) {
        return;
    }

    carriedRiseTimer_ += deltaTime;
    carriedDiveDirection_ = GetPlayerDiveDirection(player);
    player->SetMoveYaw(std::atan2(carriedDiveDirection_.x, carriedDiveDirection_.z));
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Jump);

    Vector3 velocity = player->GetVelocity();
    player->SetVelocity(velocity);

    InputManager* input = player->GetInputManager();
    const bool diveTriggered = input && input->IsMouseButtonTriggered(0) && !player->IsGrounded();
    if (diveTriggered) {
        Vector3 apexPos = player->GetWorldPosition();
        apexPos.y += 0.28f;
        SpawnMeshEffectAtSafe(kApexFlashEffectPath, apexPos, MakeDiveEffectRotation(carriedDiveDirection_, -0.18f), { 0.86f, 0.86f, 0.86f });
        BeginCarriedDive(player);
        return;
    }

    if (player->IsGrounded() && carriedRiseTimer_ > 0.16f) {
        ResetCarriedAbility(player, true);
    }
}

void EnemySlime::BeginCarriedDive(Player* player) {
    if (!player) {
        return;
    }
    if (player->IsGrounded()) {
        return;
    }

    carriedAbilityState_ = CarriedAbilityState::Dive;
    carriedDiveTimer_ = 0.0f;
    carriedDiveTrailTimer_ = 0.0f;
    carriedDiveDirection_ = GetPlayerDiveDirection(player);

    player->SetIsControlActive(false);
    player->SetDashInvincible(true);
    player->SetMoveYaw(std::atan2(carriedDiveDirection_.x, carriedDiveDirection_.z));
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Dash);
    player->SetVelocity({
        carriedDiveDirection_.x * kCarriedDiveSpeed,
        kCarriedDiveStartDownSpeed,
        carriedDiveDirection_.z * kCarriedDiveSpeed
    });
    player->TriggerSlimeImpulse({ 1.15f, 2.75f, 1.9f }, 0.16f);
}

void EnemySlime::UpdateCarriedDive(Player* player, float deltaTime) {
    if (!player) {
        return;
    }

    carriedDiveTimer_ += deltaTime;
    carriedDiveDirection_ = GetPlayerDiveDirection(player);
    const float diveRate = SmoothStep01(carriedDiveTimer_ / kCarriedDiveMaxDuration);
    const float speed = kCarriedDiveSpeed * (1.0f + diveRate * 0.30f);
    const float targetY = LerpFloat(kCarriedDiveStartDownSpeed, kCarriedDiveDropSpeed, diveRate);

    Vector3 velocity = player->GetVelocity();
    velocity.x = LerpFloat(velocity.x, carriedDiveDirection_.x * speed, deltaTime * 18.0f);
    velocity.z = LerpFloat(velocity.z, carriedDiveDirection_.z * speed, deltaTime * 18.0f);
    velocity.y = LerpFloat(velocity.y, targetY, deltaTime * 24.0f);

    player->SetIsControlActive(false);
    player->SetMoveYaw(std::atan2(carriedDiveDirection_.x, carriedDiveDirection_.z));
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Dash);
    player->SetSlimeAnimationDirection(carriedDiveDirection_);
    player->SetVelocity(velocity);
    SpawnCarriedDiveTrailEffect(player, deltaTime);

    if ((player->IsGrounded() && carriedDiveTimer_ > 0.12f && velocity.y <= 0.0f) ||
        carriedDiveTimer_ >= kCarriedDiveMaxDuration) {
        EndCarriedDive(player);
    }
}

void EnemySlime::EndCarriedDive(Player* player) {
    if (!player) {
        return;
    }

    SpawnCarriedLandingEffect(player);
    carriedAbilityState_ = CarriedAbilityState::Recover;
    carriedRecoverTimer_ = kCarriedRecoverDuration;
    carriedDiveTimer_ = 0.0f;
    carriedDiveTrailTimer_ = 0.0f;

    Vector3 velocity = player->GetVelocity();
    velocity.x *= 0.30f;
    velocity.z *= 0.30f;
    if (velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }
    player->SetVelocity(velocity);
    player->SetIsControlActive(true);
    player->SetDashInvincible(false);
    player->SetSlimeJumpCharge(0.0f);
    player->TriggerSlimeImpulse({ 3.0f, 0.62f, 2.65f }, 0.20f);
    if (player->IsGrounded()) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
    } else {
        player->ChangeState(std::make_unique<PlayerStateJump>());
    }
}

void EnemySlime::BeginCarriedThrust(Player* player) {
    if (!player || !player->IsPinkSlimeMorphed() || carriedAbilityState_ != CarriedAbilityState::Idle) {
        return;
    }

    carriedAbilityState_ = CarriedAbilityState::Thrust;
    carriedDiveTimer_ = 0.0f;
    carriedDiveTrailTimer_ = 0.0f;
    carriedDiveDirection_ = GetPlayerDiveDirection(player);

    Vector3 velocity = player->GetVelocity();
    velocity.x = carriedDiveDirection_.x * kCarriedThrustSpeed;
    velocity.z = carriedDiveDirection_.z * kCarriedThrustSpeed;
    if (player->IsGrounded() && velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }

    player->SetIsControlActive(false);
    player->SetDashInvincible(true);
    player->SetMoveYaw(std::atan2(carriedDiveDirection_.x, carriedDiveDirection_.z));
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Dash);
    player->SetSlimeAnimationDirection(carriedDiveDirection_);
    player->SetVelocity(velocity);
    player->TriggerSlimeImpulse({ 1.15f, 2.35f, 1.85f }, 0.14f);

    Vector3 position = player->GetWorldPosition();
    position.y += 0.45f;
    SpawnMeshEffectAtSafe(kDiveTrailEffectPath, position, MakeDiveEffectRotation(carriedDiveDirection_, -0.30f), { 0.70f, 0.70f, 1.65f });
}

void EnemySlime::UpdateCarriedThrust(Player* player, float deltaTime) {
    if (!player) {
        return;
    }

    carriedDiveTimer_ += deltaTime;
    const float rate = SmoothStep01(carriedDiveTimer_ / kCarriedThrustDuration);
    const float speed = LerpFloat(kCarriedThrustSpeed, kCarriedThrustSpeed * 0.34f, rate);

    Vector3 velocity = player->GetVelocity();
    velocity.x = carriedDiveDirection_.x * speed;
    velocity.z = carriedDiveDirection_.z * speed;
    if (player->IsGrounded() && velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }

    player->SetIsControlActive(false);
    player->SetDashInvincible(true);
    player->SetMoveYaw(std::atan2(carriedDiveDirection_.x, carriedDiveDirection_.z));
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Dash);
    player->SetSlimeAnimationDirection(carriedDiveDirection_);
    player->SetVelocity(velocity);
    SpawnCarriedDiveTrailEffect(player, deltaTime);

    if (carriedDiveTimer_ >= kCarriedThrustDuration) {
        velocity.x *= 0.26f;
        velocity.z *= 0.26f;
        player->SetVelocity(velocity);
        player->SetIsControlActive(true);
        player->SetDashInvincible(false);
        if (player->IsGrounded()) {
            player->ChangeState(std::make_unique<PlayerStateIdle>());
        } else {
            player->ChangeState(std::make_unique<PlayerStateJump>());
        }
        ResetCarriedAbility(player, true);
    }
}

void EnemySlime::ResetCarriedAbility(Player* player, bool restoreControl) {
    carriedAbilityState_ = CarriedAbilityState::Idle;
    carriedChargeTimer_ = 0.0f;
    carriedRiseTimer_ = 0.0f;
    carriedDiveTimer_ = 0.0f;
    carriedRecoverTimer_ = 0.0f;
    carriedChargeEffectTimer_ = 0.0f;
    carriedDiveTrailTimer_ = 0.0f;

    if (player) {
        player->SetSlimeJumpCharge(0.0f);
        player->SetDashInvincible(false);
        if (restoreControl) {
            player->SetIsControlActive(true);
        }
        if (!player->IsInvincible()) {
            const Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
            player->SetColor(color);
            for (Object3d* child : player->GetChildren()) {
                if (child) {
                    child->SetColor(color);
                }
            }
        }
    }
}

Vector3 EnemySlime::GetPlayerDiveDirection(Player* player) const {
    if (!player) {
        return { 0.0f, 0.0f, 1.0f };
    }

    Vector3 direction = player->GetForwardDirection();
    direction.y = 0.0f;
    return NormalizePlanarOr(direction, carriedDiveDirection_);
}

void EnemySlime::SpawnCarriedChargeEffect(Player* player, float deltaTime, float chargeRate) {
    if (!player) {
        return;
    }

    carriedChargeEffectTimer_ -= deltaTime;
    if (carriedChargeEffectTimer_ > 0.0f) {
        return;
    }
    carriedChargeEffectTimer_ = (std::max)(0.18f, 0.34f - chargeRate * 0.10f);

    Vector3 position = ResolveSlimeGroundEffectPosition(player->GetWorldPosition(), 0.06f);
    const float scale = 0.94f + chargeRate * 0.80f;
    SpawnMeshEffectAtSafe(kChargePulseEffectPath, position, { 0.0f, 0.0f, 0.0f }, { scale, 0.9f, scale });

    if (chargeRate > 0.42f) {
        Vector3 corePosition = player->GetWorldPosition();
        corePosition.y += 0.72f;
        SpawnMeshEffectAtSafe(kChargeCoreEffectPath, corePosition, MakeDiveEffectRotation(carriedDiveDirection_, -0.10f), { 0.36f + chargeRate * 0.34f, 0.36f + chargeRate * 0.34f, 0.36f + chargeRate * 0.34f });
    }
}

void EnemySlime::SpawnCarriedDiveTrailEffect(Player* player, float deltaTime) {
    if (!player) {
        return;
    }

    carriedDiveTrailTimer_ -= deltaTime;
    if (carriedDiveTrailTimer_ > 0.0f) {
        return;
    }
    carriedDiveTrailTimer_ = kCarriedDiveTrailInterval;

    Vector3 position = player->GetWorldPosition();
    position.x -= carriedDiveDirection_.x * 0.60f;
    position.y += 0.42f;
    position.z -= carriedDiveDirection_.z * 0.60f;
    const float diveRate = SmoothStep01(carriedDiveTimer_ / kCarriedDiveMaxDuration);
    SpawnMeshEffectAtSafe(kDiveTrailEffectPath, position, MakeDiveEffectRotation(carriedDiveDirection_, -0.52f), { 0.62f, 0.62f, 1.45f + diveRate * 0.80f });
}

void EnemySlime::SpawnCarriedLandingEffect(Player* player) {
    if (!player) {
        return;
    }

    Vector3 position = ResolveSlimeGroundEffectPosition(player->GetWorldPosition(), 0.06f);
    SpawnDebrisOnGroundSafe(kLandingDebrisPresetName, player->GetWorldPosition(), 0.05f);
    SpawnMeshEffectAtSafe(kLandingRingEffectPath, position, { 0.0f, 0.0f, 0.0f }, { 1.95f, 0.95f, 1.95f });
    Vector3 flashPosition = position;
    flashPosition.y += 0.34f;
    SpawnMeshEffectAtSafe(kLandingCoreEffectPath, flashPosition, MakeDiveEffectRotation(carriedDiveDirection_, -0.42f), { 1.14f, 1.14f, 1.14f });
}

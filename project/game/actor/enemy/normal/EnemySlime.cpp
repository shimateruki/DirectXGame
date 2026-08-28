#include "EnemySlime.h"
#include "SlimeBounceAnimator.h"
#include "DebrisEffectManager.h"
#include "HitEffectDirector.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "engine/utility/math/Math.h"

#include <DebugConsole.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kDiveAttackId = "dive_slam";
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
constexpr float kChargePulseInterval = 0.56f;
constexpr float kChargeDebrisInterval = 0.64f;
constexpr float kChargeVortexInterval = 0.34f;
constexpr float kRiseJumpSpeed = 32.0f;
constexpr float kRiseApexMinTime = 0.16f;
constexpr float kRiseMaxDuration = 1.15f;
constexpr float kDiveDistanceSpeedRate = 0.72f;
constexpr float kDiveSteerStrength = 5.5f;
constexpr float kDiveStartDownSpeed = -16.0f;
constexpr float kDiveDropSpeed = -62.0f;
constexpr float kDiveTrailInterval = 0.12f;
constexpr float kWanderHopInterval = 1.15f;
constexpr float kLandingSquashDuration = 0.20f;

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

void EmitGpuPresetSafe(const char* presetName, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized()) {
        particles->Emit(presetName, position);
    }
}

Object3d* FindEnemyDamageTarget(Object3d* object) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        if (dynamic_cast<BaseEnemy*>(current)) {
            return current;
        }
    }
    return nullptr;
}

void EnemySlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetEnemyType("Slime");
    ReloadAttackProfile();
    ResetVisualPose();
}

void EnemySlime::ApplyManagedScale(const Vector3& scale) {
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
    ResetVisualPose();
}

void EnemySlime::Update(float deltaTime) {
    if (IsDormant()) {
        BaseEnemy::Update(deltaTime);
        return;
    }

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
    const EnemyAttackDefinition& attack = GetAttackDefinition(kDiveAttackId);
    effectiveDetectionRange = (std::min)(effectiveDetectionRange, attack.maxRange);
    const bool canDiveAtTarget = targetDistance <= effectiveDetectionRange && targetDistance >= attack.minRange;

    if (moveState_ == MoveState::Wander && UpdateNoticeReaction(deltaTime, targetDistance, effectiveDetectionRange, targetDirection)) {
        FaceDirection(targetDirection, deltaTime * 10.0f);
        SetVelocity({ 0.0f, GetVelocity().y, 0.0f });
        ApplySlimeAnimation(deltaTime);
        BaseEnemy::Update(deltaTime);
        return;
    }

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

void EnemySlime::ResetVisualPose() {
    visualScale_ = { 1.0f, 1.0f, 1.0f };
    visualScaleVelocity_ = { 0.0f, 0.0f, 0.0f };
    visualRotation_ = { 0.0f, 0.0f, 0.0f };
    visualRotationVelocity_ = { 0.0f, 0.0f, 0.0f };
    if (MeshRenderer* renderer = GetMeshRenderer()) {
        renderer->ResetVisualTransform();
    }
}

Vector3 EnemySlime::CalculateGroundedVisualOffset(const Vector3& visualScale) const {
    const Model* model = GetModel();
    if (!model) {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Vector3 modelCenter = model->GetCenter();
    const Vector3 modelSize = model->GetSize();
    const float modelBottom = modelCenter.y - modelSize.y * 0.5f;
    return { 0.0f, modelBottom * (1.0f - visualScale.y), 0.0f };
}

Vector3 EnemySlime::CalculateDiveVisualOffset(const Vector3& visualScale, const Vector3& visualRotation) const {
    const Model* model = GetModel();
    if (!model) {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Vector3 modelCenter = model->GetCenter();
    const Vector3 modelSize = model->GetSize();
    // ピンクスライムのモデル正面はローカル-Zです。顔側を固定し、伸びを後方へ逃がします。
    const Vector3 frontAnchor = {
        modelCenter.x,
        modelCenter.y,
        modelCenter.z - modelSize.z * 0.5f
    };
    const Matrix4x4 visualMatrix = Math::MakeAffineMatrix(
        visualScale, visualRotation, { 0.0f, 0.0f, 0.0f });
    const Vector3 deformedAnchor = Math::Transform(frontAnchor, visualMatrix);
    return frontAnchor - deformedAnchor;
}

const char* EnemySlime::GetDebugMoveStateName() const {
    switch (moveState_) {
    case MoveState::Charge:
        return "溜め";
    case MoveState::Rise:
        return "上昇";
    case MoveState::Dive:
        return "突進";
    case MoveState::Recover:
        return "着地反発";
    case MoveState::Wander:
    default:
        return "待機・移動";
    }
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
    attackWarningTriggered_ = false;

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
    moveState_ = MoveState::Charge;
    jumpTimer_ = 0.0f;
    diveDirection_ = NormalizePlanarOr(direction, diveDirection_);

    const EnemyAttackDefinition& attack = GetAttackDefinition(kDiveAttackId);
    const float chargeDuration = (std::max)(0.01f, attack.windupDuration);
    const float chargeRate = Clamp01(chargeTimer_ / chargeDuration);
    const Vector3 telegraphCenter = ResolveSlimeGroundEffectPosition(GetTranslate(), 0.05f);
    const float predictedDistance = std::clamp(
        distance + attack.radius,
        (std::max)(2.4f, attack.minRange),
        attack.maxRange);
    ShowAttackTelegraphLine(
        telegraphCenter,
        diveDirection_,
        predictedDistance,
        attack.radius * 2.0f,
        chargeRate,
        { 1.0f, 0.24f, 0.76f, 0.84f });
    SpawnChargePulseEffect(deltaTime, chargeRate);
    SpawnChargeDebrisEffect(deltaTime, chargeRate);
    SpawnChargeVortexEffect(deltaTime, chargeRate);
    FaceDirection(diveDirection_, 0.11f + chargeRate * 0.10f);

    velocity_.x = 0.0f;
    velocity_.z = 0.0f;
    chargeTimer_ += deltaTime;

    const float remainingChargeTime = (std::max)(0.0f, chargeDuration - chargeTimer_);
    if (!attackWarningTriggered_ && remainingChargeTime <= attack.warningLeadTime) {
        TriggerAttackTelegraphCue({ 1.0f, 0.34f, 0.78f, 1.0f });
        attackWarningTriggered_ = true;
    }

    if (chargeTimer_ >= chargeDuration) {
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
    const EnemyAttackDefinition& attack = GetAttackDefinition(kDiveAttackId);
    diveSpeed_ = std::clamp(attack.minSpeed + speedFromDistance + speedFromStatus, attack.minSpeed, attack.maxSpeed);

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

    const float activeDuration = (std::max)(0.01f, GetAttackDefinition(kDiveAttackId).activeDuration);
    const float curveRate = SmoothStep01(diveTimer_ / activeDuration);
    const float targetSpeed = diveSpeed_ * (1.0f + curveRate * 0.34f);
    const float velocityRate = deltaTime * (kDiveSteerStrength + curveRate * 5.5f);
    velocity_.x = LerpFloat(velocity_.x, diveDirection_.x * targetSpeed, velocityRate);
    velocity_.z = LerpFloat(velocity_.z, diveDirection_.z * targetSpeed, velocityRate);

    const float diveRate = SmoothStep01(diveTimer_ / activeDuration);
    const float targetYSpeed = LerpFloat(kDiveStartDownSpeed, kDiveDropSpeed, diveRate);
    velocity_.y = LerpFloat(velocity_.y, targetYSpeed, deltaTime * 22.0f);

    if (diveTimer_ > activeDuration) {
        diveTimer_ = activeDuration;
    }
}

void EnemySlime::BeginLandingRecovery() {
    moveState_ = MoveState::Recover;
    recoverTimer_ = GetAttackDefinition(kDiveAttackId).recoveryDuration;
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
    const EnemyAttackDefinition& attack = GetAttackDefinition(kDiveAttackId);
    const char* windupVfx = attack.windupVfx.empty() ? kChargePulseEffectPath : attack.windupVfx.c_str();
    SpawnMeshEffectAtSafe(windupVfx, position, { 0.0f, 0.0f, 0.0f }, { scale, 0.9f, scale });

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
    const EnemyAttackDefinition& attack = GetAttackDefinition(kDiveAttackId);
    const float activeDuration = (std::max)(0.01f, attack.activeDuration);
    const float diveRate = SmoothStep01(diveTimer_ / activeDuration);
    const float scaleZ = 1.35f + diveRate * 0.80f;
    const char* activeVfx = attack.activeVfx.empty() ? kDiveTrailEffectPath : attack.activeVfx.c_str();
    SpawnMeshEffectAtSafe(activeVfx, position, MakeDiveEffectRotation(diveDirection_, -0.52f), { 0.58f, 0.58f, scaleZ });
}

void EnemySlime::SpawnLandingEffect() {
    Vector3 position = ResolveSlimeGroundEffectPosition(GetTranslate(), 0.06f);
    SpawnDebrisOnGroundSafe(kLandingDebrisPresetName, GetTranslate(), 0.05f);
    const EnemyAttackDefinition& attack = GetAttackDefinition(kDiveAttackId);
    const char* impactVfx = attack.impactVfx.empty() ? kLandingRingEffectPath : attack.impactVfx.c_str();
    SpawnMeshEffectAtSafe(impactVfx, position, { 0.0f, 0.0f, 0.0f }, { 1.85f, 0.95f, 1.85f });
    Vector3 flashPosition = position;
    flashPosition.y += 0.34f;
    SpawnMeshEffectAtSafe(kLandingCoreEffectPath, flashPosition, MakeDiveEffectRotation(diveDirection_, -0.42f), { 1.10f, 1.10f, 1.10f });
    SpawnMeshEffectAtSafe(kLandingShockArcEffectPath, position, MakeDiveEffectRotation(diveDirection_, -0.04f), { 1.42f, 1.42f, 1.42f });
    SpawnMeshEffectAtSafe(kLandingShockArcEffectPath, position, MakeDiveEffectRotation({ -diveDirection_.z, 0.0f, diveDirection_.x }, -0.04f), { 1.16f, 1.16f, 1.16f });
}

void EnemySlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetVisualScale = { 1.0f, 1.0f, 1.0f };
    Vector3 targetVisualRotation = { 0.0f, 0.0f, 0.0f };
    SlimeBounceAnimator::Params bounceParams;
    bounceParams.speedForFullBounce = 2.2f;
    bounceParams.idleAmplitude = 0.055f;
    bounceParams.moveAmplitude = 0.22f;
    bounceParams.hopFrequency = 11.2f;
    bounceParams.horizontalSquash = 0.24f;
    bounceParams.verticalStretch = 0.31f;
    bounceParams.airborneStretch = 0.30f;

    const auto toVisualScale = [this](const Vector3& absoluteScale) {
        return Vector3{
            std::abs(baseScale_.x) > 0.0001f ? absoluteScale.x / baseScale_.x : 1.0f,
            std::abs(baseScale_.y) > 0.0001f ? absoluteScale.y / baseScale_.y : 1.0f,
            std::abs(baseScale_.z) > 0.0001f ? absoluteScale.z / baseScale_.z : 1.0f
        };
    };

    if (moveState_ == MoveState::Charge) {
        const float windupDuration = (std::max)(0.01f, GetAttackDefinition(kDiveAttackId).windupDuration);
        const float chargeRate = SmoothStep01(chargeTimer_ / windupDuration);
        const float finalCompression = SmoothStep01((chargeRate - 0.76f) / 0.24f);
        const float tremble = std::sin(idleTimer_ * 31.0f) * 0.018f * chargeRate;
        targetVisualScale = {
            1.0f + chargeRate * 0.25f + finalCompression * 0.08f + tremble,
            1.0f - chargeRate * 0.24f - finalCompression * 0.12f,
            1.0f + chargeRate * 0.21f + finalCompression * 0.07f - tremble * 0.70f
        };
        const float wobble = std::sin(idleTimer_ * 27.0f) * chargeRate * 0.055f;
        targetVisualRotation.x = -diveDirection_.z * wobble;
        targetVisualRotation.z = diveDirection_.x * wobble;
    } else if (moveState_ == MoveState::Rise) {
        const float release = 1.0f - SmoothStep01(riseTimer_ / 0.30f);
        const float verticalSpeedRate = Clamp01(std::abs(velocity_.y) / kRiseJumpSpeed);
        const float riseStretch = 0.34f + verticalSpeedRate * 0.16f + release * 0.10f;
        targetVisualScale = {
            1.0f - riseStretch * 0.42f,
            1.0f + riseStretch,
            1.0f - riseStretch * 0.34f
        };
        targetVisualRotation.x = -0.08f * release;
    } else if (moveState_ == MoveState::Dive) {
        const float activeDuration = (std::max)(0.01f, GetAttackDefinition(kDiveAttackId).activeDuration);
        const float divePose = SmoothStep01(diveTimer_ / activeDuration);
        const float planarSpeed = PlanarLength(velocity_);
        const float totalSpeed = std::sqrt(planarSpeed * planarSpeed + velocity_.y * velocity_.y);
        const float speedRate = Clamp01((totalSpeed - 18.0f) / 38.0f);
        const float stretchRate = 0.52f + divePose * 0.48f;
        const float longitudinalScale = 1.38f + stretchRate * 0.38f + speedRate * 0.16f;
        const float crossScale = 1.0f / std::sqrt(longitudinalScale);
        const float flutter = std::sin(diveTimer_ * 42.0f) * 0.025f * divePose;
        targetVisualScale = {
            crossScale + flutter,
            crossScale * 0.94f - flutter * 0.45f,
            longitudinalScale
        };
        const float flightPitch = std::atan2(velocity_.y, (std::max)(planarSpeed, 0.001f));
        targetVisualRotation.x = std::clamp(flightPitch, -1.10f, 0.30f);
        targetVisualRotation.z = flutter * 1.8f;
    } else if (moveState_ == MoveState::Recover) {
        const float recoveryDuration = (std::max)(0.01f, GetAttackDefinition(kDiveAttackId).recoveryDuration);
        const float recoveryProgress = Clamp01((recoveryDuration - recoverTimer_) / recoveryDuration);
        if (recoveryProgress < 0.30f) {
            const float phase = SmoothStep01(recoveryProgress / 0.30f);
            targetVisualScale = {
                LerpFloat(1.40f, 1.23f, phase),
                LerpFloat(0.56f, 0.72f, phase),
                LerpFloat(1.32f, 1.18f, phase)
            };
        } else if (recoveryProgress < 0.64f) {
            const float phase = SmoothStep01((recoveryProgress - 0.30f) / 0.34f);
            targetVisualScale = {
                LerpFloat(1.23f, 0.92f, phase),
                LerpFloat(0.72f, 1.24f, phase),
                LerpFloat(1.18f, 0.94f, phase)
            };
        } else {
            const float phase = SmoothStep01((recoveryProgress - 0.64f) / 0.36f);
            targetVisualScale = {
                LerpFloat(0.92f, 1.0f, phase),
                LerpFloat(1.24f, 1.0f, phase),
                LerpFloat(0.94f, 1.0f, phase)
            };
        }
        const float shake = std::sin(idleTimer_ * 34.0f) * (1.0f - recoveryProgress) * 0.055f;
        targetVisualRotation.x = -diveDirection_.z * shake;
        targetVisualRotation.z = diveDirection_.x * shake;
    } else if (!isGrounded_) {
        targetVisualScale = toVisualScale(SlimeBounceAnimator::MakeScale(baseScale_, velocity_, idleTimer_, false, bounceParams));
    } else {
        targetVisualScale = toVisualScale(SlimeBounceAnimator::MakeScale(baseScale_, velocity_, idleTimer_, true, bounceParams));
    }

    targetVisualScale.x = std::clamp(targetVisualScale.x, 0.58f, 1.45f);
    targetVisualScale.y = std::clamp(targetVisualScale.y, 0.48f, 1.55f);
    targetVisualScale.z = std::clamp(targetVisualScale.z, 0.62f, 1.96f);

    SlimeBounceAnimator::StepDampedSpring(
        visualScale_, visualScaleVelocity_, targetVisualScale, deltaTime, 23.0f, 0.58f);
    SlimeBounceAnimator::StepDampedSpring(
        visualRotation_, visualRotationVelocity_, targetVisualRotation, deltaTime, 21.0f, 0.68f);

    visualScale_.x = std::clamp(visualScale_.x, 0.58f, 1.50f);
    visualScale_.y = std::clamp(visualScale_.y, 0.44f, 1.60f);
    visualScale_.z = std::clamp(visualScale_.z, 0.58f, 2.02f);

    Vector3 renderedScale = visualScale_;
    Vector3 renderedRotation = visualRotation_;
    Vector3 reactionOffset = { 0.0f, 0.0f, 0.0f };
    ApplyDamageReactionPose(renderedScale, renderedRotation, &reactionOffset);
    renderedScale.x = std::clamp(renderedScale.x, 0.52f, 1.58f);
    renderedScale.y = std::clamp(renderedScale.y, 0.40f, 1.68f);
    renderedScale.z = std::clamp(renderedScale.z, 0.52f, 2.10f);

    SetScale(baseScale_);
    const float yaw = GetRotation().y;
    SetRotation({ 0.0f, yaw, 0.0f });

    if (MeshRenderer* renderer = GetMeshRenderer()) {
        const bool keepGroundContact = isGrounded_ || moveState_ == MoveState::Charge || moveState_ == MoveState::Recover;
        Vector3 visualOffset = { 0.0f, 0.0f, 0.0f };
        if (moveState_ == MoveState::Dive) {
            visualOffset = CalculateDiveVisualOffset(renderedScale, renderedRotation);
        } else if (keepGroundContact) {
            visualOffset = CalculateGroundedVisualOffset(renderedScale);
        }
        visualOffset.x += reactionOffset.x;
        visualOffset.y += reactionOffset.y;
        visualOffset.z += reactionOffset.z;
        renderer->SetVisualTransform(renderedScale, renderedRotation, visualOffset);
    }
}

std::unique_ptr<Object3d> EnemySlime::Clone() const {
    auto newSlime = std::make_unique<EnemySlime>();
    newSlime->Initialize(common_, this->GetModelName());
    newSlime->CopyFrom(this);
    newSlime->SetTarget(this->target_);
    newSlime->SetDetectionRange(this->detectionRange_);
    return newSlime;
}

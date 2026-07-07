#include "EnemyBomber.h"
#include "SlimeBounceAnimator.h"
#include "CollisionConfig.h"
#include "EnemyBomb.h"
#include "EnemyFactory.h"
#include "Player.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include <algorithm>
#include <cmath>

namespace {
// ボマーの距離維持、予備動作、投げ演出に関する調整値。
constexpr float kMinThrowDistance = 1.0f;
constexpr float kBombSpawnHeight = 2.0f;
constexpr float kFootworkSpeed = 1.25f;
constexpr float kPreferredDistance = 13.0f;
constexpr float kBombTelegraphRadius = 2.35f;
constexpr float kCarryThrowInterval = 0.42f;
constexpr float kCarryBombForwardSpeed = 24.0f;
constexpr float kCarryBombUpSpeed = 8.0f;
constexpr float kThrowRecoilDuration = 0.36f;
constexpr float kThrowRecoverPoseDuration = 0.62f;
constexpr float kThrowLeapDuration = 0.78f;
constexpr float kThrowLandingDuration = 0.34f;
constexpr float kThrowReleaseProgress = 0.52f;
constexpr float kThrowJumpVelocity = 24.0f;
constexpr float kThrowTakeoffLift = 0.18f;
constexpr float kThrowHopForwardSpeed = 3.2f;
constexpr float kHeldBombScale = 0.30f;
constexpr float kBomberModelYawOffset = 3.1415926535f;
constexpr const char* kHeldBombModelName = "Gimmicks/blob";
constexpr const char* kCarryBomberThrowEffect = "Resources/json/effect/effect_carry_bomber_throw_burst.json";
constexpr const char* kCarryBomberSparkPreset = "carry_bomber_throw_sparks";
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = kPi * 2.0f;

float Saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SmoothStep01(float value) {
    value = Saturate(value);
    return value * value * (3.0f - 2.0f * value);
}

float LerpFloat(float start, float end, float rate) {
    return start + (end - start) * rate;
}
}
void EnemyBomber::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    common_ = common;
    throwTimer_ = initialThrowDelay_;
    windupTimer_ = 0.0f;
    footworkTimer_ = 1.2f;
    footworkDirection_ = 1.0f;
    throwState_ = ThrowState::Idle;
    throwRecoilTimer_ = 0.0f;
    throwRecoverPoseTimer_ = 0.0f;
    throwLeapTimer_ = 0.0f;
    throwLandingTimer_ = 0.0f;
    throwLeapBaseYaw_ = 0.0f;
    throwSpinDirection_ = 1.0f;
    throwBombReleased_ = false;

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kPlayerAttack);
}

// 距離を保ちながら、爆弾を取り出して跳び投げるAI。
void EnemyBomber::Update(float deltaTime) {
    if (UpdateBlockedState(deltaTime)) {
        return;
    }

    EnsureBaseScaleForAnimation();
    UpdateThrowTimers(deltaTime);

    float distance = 0.0f;
    Vector3 direction{};
    const bool inRange = IsTargetInRange(&distance, &direction);
    if (inRange && throwState_ != ThrowState::LeapThrow) {
        UpdateFacing(direction);
    }

    UpdateCombatMovement(deltaTime, direction, distance, inRange);
    UpdateThrowState(deltaTime, inRange);
    ApplySlimeAnimation(deltaTime);
    BaseEnemy::Update(deltaTime);
}

bool EnemyBomber::UpdateBlockedState(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        HideHeldBombVisual();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    if (isCarried_) {
        HideHeldBombVisual();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    if (!target_ || isDead) {
        HideHeldBombVisual();
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    if (IsThrowRecovering()) {
        HideHeldBombVisual();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

void EnemyBomber::EnsureBaseScaleForAnimation() {
    if (hasBaseScale_) {
        return;
    }

    baseScale_ = GetScale();
    const float capturedMaxScale = (std::max)({ std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z) });
    if (capturedMaxScale < 1.2f) {
        baseScale_ = { 2.0f, 2.0f, 2.0f };
        SetScale(baseScale_);
    }
    hasBaseScale_ = true;
}

void EnemyBomber::UpdateThrowTimers(float deltaTime) {
    idleTimer_ += deltaTime;
    throwRecoilTimer_ = std::max(0.0f, throwRecoilTimer_ - deltaTime);
    throwRecoverPoseTimer_ = std::max(0.0f, throwRecoverPoseTimer_ - deltaTime);
}

void EnemyBomber::UpdateCombatMovement(float deltaTime, const Vector3& direction, float distance, bool inRange) {
    if (throwState_ == ThrowState::Idle) {
        UpdateFootwork(deltaTime, direction, distance, inRange);
    } else if (throwState_ == ThrowState::Windup) {
        Vector3 velocity = GetVelocity();
        velocity.x *= 0.18f;
        velocity.z *= 0.18f;
        SetVelocity(velocity);
    } else if (throwState_ == ThrowState::Landing) {
        Vector3 velocity = GetVelocity();
        velocity.x *= 0.58f;
        velocity.z *= 0.58f;
        SetVelocity(velocity);
    }
}

void EnemyBomber::UpdateThrowState(float deltaTime, bool inRange) {
    if (!inRange && throwState_ == ThrowState::Idle) {
        ResetThrowStateAfterTargetLost();
        return;
    }

    switch (throwState_) {
    case ThrowState::Idle:
        HideHeldBombVisual();
        throwTimer_ -= deltaTime;
        if (throwTimer_ <= 0.0f) {
            BeginThrow();
        }
        break;
    case ThrowState::Windup:
        windupTimer_ -= deltaTime;
        {
            Vector3 targetPos = target_->GetTranslate();
            targetPos.y = GetTranslate().y;
            const float progress = GetThrowProgress();
            ShowAttackTelegraphCircle(
                targetPos,
                kBombTelegraphRadius,
                progress,
                { 0.05f, 0.045f, 0.04f, 0.62f });
            UpdateHeldBombVisual(deltaTime);
        }
        if (windupTimer_ <= 0.0f) {
            BeginThrowLeap();
        }
        break;
    case ThrowState::LeapThrow:
        UpdateThrowLeap(deltaTime);
        break;
    case ThrowState::Landing:
        HideHeldBombVisual();
        HideAttackTelegraph();
        throwLandingTimer_ -= deltaTime;
        if (throwLandingTimer_ <= 0.0f) {
            throwTimer_ = throwInterval_;
            throwState_ = ThrowState::Idle;
            throwBombReleased_ = false;
        }
        break;
    }
}

void EnemyBomber::ResetThrowStateAfterTargetLost() {
    throwState_ = ThrowState::Idle;
    windupTimer_ = 0.0f;
    throwLeapTimer_ = 0.0f;
    throwLandingTimer_ = 0.0f;
    throwBombReleased_ = false;
    throwTimer_ = std::min(throwTimer_, initialThrowDelay_);
    HideHeldBombVisual();
    HideAttackTelegraph();
}

void EnemyBomber::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    BaseEnemy::Draw(pointLightResource, spotLightResource);
    if (heldBombVisual_ && heldBombVisual_->GetIsVisible()) {
        heldBombVisual_->Draw(pointLightResource, spotLightResource);
    }
}

void EnemyBomber::SetCarried(bool isCarried) {
    BaseEnemy::SetCarried(isCarried);
    carriedThrowCooldown_ = 0.0f;
    carriedEffectTimer_ = 0.0f;
    throwState_ = ThrowState::Idle;
    windupTimer_ = 0.0f;
    throwLeapTimer_ = 0.0f;
    throwLandingTimer_ = 0.0f;
    throwBombReleased_ = false;
    HideAttackTelegraph();
}
void EnemyBomber::ExecuteAbility(Player* player) {
    if (!player || !isCarried_ || carriedThrowCooldown_ > 0.0f) {
        return;
    }

    ThrowCarryBomb(player);
    carriedThrowCooldown_ = kCarryThrowInterval;
    carriedEffectTimer_ = 0.18f;
}

void EnemyBomber::UpdateCarriedAbility(Player* player, float deltaTime) {
    (void)player;
    if (!isCarried_) {
        return;
    }

    carriedThrowCooldown_ = std::max(0.0f, carriedThrowCooldown_ - deltaTime);
    carriedEffectTimer_ = std::max(0.0f, carriedEffectTimer_ - deltaTime);
    HideAttackTelegraph();
}
bool EnemyBomber::IsTargetInRange(float* outDistance, Vector3* outDirection) const {
    if (!target_) return false;

    Vector3 toTarget = target_->GetTranslate() - GetTranslate();
    toTarget.y = 0.0f;

    const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (outDistance) {
        *outDistance = distance;
    }

    if (distance > 0.001f) {
        toTarget.x /= distance;
        toTarget.z /= distance;
    } else {
        toTarget = { 0.0f, 0.0f, 1.0f };
    }

    if (outDirection) {
        *outDirection = toTarget;
    }

    return distance <= detectionRange_ && distance >= kMinThrowDistance;
}

void EnemyBomber::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq > 0.0001f) {
        const float targetYaw = std::atan2(direction.x, direction.z) + kBomberModelYawOffset;
        SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.16f));
    }
}

void EnemyBomber::ApplySlimeAnimation(float deltaTime) {
    SlimeBounceAnimator::Params params;
    params.speedForFullBounce = 1.15f;
    params.idleAmplitude = 0.075f;
    params.moveAmplitude = 0.28f;
    params.hopFrequency = 9.8f;
    params.horizontalSquash = 0.28f;
    params.verticalStretch = 0.34f;
    params.airborneStretch = 0.30f;

    Vector3 targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    if (throwState_ == ThrowState::Windup) {
        const float chargeRate = GetThrowProgress();
        targetScale = SlimeBounceAnimator::MakeChargeSquash(baseScale_, chargeRate, idleTimer_, 0.92f);
    } else if (throwState_ == ThrowState::LeapThrow) {
        targetScale = baseScale_;
    } else if (throwState_ == ThrowState::Landing) {
        const float remainRate = Saturate(throwLandingTimer_ / kThrowLandingDuration);
        const float impact = SmoothStep01(remainRate);
        const float rebound = std::sin((1.0f - remainRate) * kPi) * 0.08f;
        targetScale.x = baseScale_.x * (1.0f + impact * 0.20f - rebound * 0.25f);
        targetScale.y = baseScale_.y * (1.0f - impact * 0.24f + rebound);
        targetScale.z = baseScale_.z * (1.0f + impact * 0.22f - rebound * 0.25f);
    }
    if (throwRecoilTimer_ > 0.0f && throwState_ == ThrowState::Idle) {
        const float recoilRate = 1.0f - Saturate(throwRecoilTimer_ / kThrowRecoilDuration);
        const float kick = std::sin(recoilRate * kPi);
        targetScale.x *= 1.0f - kick * 0.10f;
        targetScale.y *= 1.0f + kick * 0.12f;
        targetScale.z *= 1.0f + kick * 0.26f;
    }

    if (throwRecoverPoseTimer_ > 0.0f && throwState_ == ThrowState::Idle) {
        const float remainingRate = Saturate(throwRecoverPoseTimer_ / kThrowRecoverPoseDuration);
        const float wave = std::sin((1.0f - remainingRate) * kPi * 3.0f) * remainingRate;
        targetScale.x *= 1.0f + remainingRate * 0.10f + wave * 0.04f;
        targetScale.y *= 1.0f - remainingRate * 0.12f;
        targetScale.z *= 1.0f + remainingRate * 0.18f + wave * 0.06f;
    }

    Vector3 animatedRotation = GetRotation();
    float yaw = animatedRotation.y;
    float pitch = 0.0f;
    float roll = 0.0f;
    if (throwState_ == ThrowState::Windup) {
        const float progress = GetThrowProgress();
        const float liftRate = SmoothStep01(progress / 0.55f);
        pitch += LerpFloat(0.0f, -0.20f, liftRate);
        roll += std::sin(progress * kPi) * 0.10f * footworkDirection_;
    } else if (throwState_ == ThrowState::LeapThrow) {
        const float progress = GetThrowLeapProgress();
        const float spinRate = SmoothStep01(progress);
        const float arcRate = std::sin(progress * kPi);
        yaw = throwLeapBaseYaw_;
        pitch = -spinRate * kTwoPi - 0.08f * arcRate;
        roll = std::sin(progress * kPi) * 0.06f * throwSpinDirection_;
    } else if (throwState_ == ThrowState::Landing) {
        const float remainRate = Saturate(throwLandingTimer_ / kThrowLandingDuration);
        pitch = -0.12f * remainRate;
        roll = std::sin((1.0f - remainRate) * kPi) * 0.08f * throwSpinDirection_;
    }
    if (throwRecoilTimer_ > 0.0f && throwState_ != ThrowState::LeapThrow) {
        const float recoilRate = 1.0f - Saturate(throwRecoilTimer_ / kThrowRecoilDuration);
        const float kick = std::sin(recoilRate * kPi);
        pitch -= kick * 0.22f;
        roll += kick * 0.12f * footworkDirection_;
    }
    if (throwRecoverPoseTimer_ > 0.0f && throwState_ == ThrowState::Idle) {
        const float remainingRate = Saturate(throwRecoverPoseTimer_ / kThrowRecoverPoseDuration);
        roll += std::sin((1.0f - remainingRate) * kPi * 2.0f) * remainingRate * 0.10f;
    }
    SetRotation({ pitch, yaw, roll });
    SetScale(Math::Lerp(GetScale(), targetScale, (std::min)(1.0f, deltaTime * 12.0f)));
}
void EnemyBomber::UpdateHeldBombVisual(float deltaTime) {
    if ((throwState_ != ThrowState::Windup && throwState_ != ThrowState::LeapThrow) || isCarried_ || throwBombReleased_) {
        HideHeldBombVisual();
        return;
    }

    EnsureHeldBombVisual();
    if (!heldBombVisual_) {
        return;
    }

    const float progress = (throwState_ == ThrowState::LeapThrow) ? 1.0f : GetThrowProgress();
    const float appearRate = SmoothStep01(progress / 0.28f);
    if (appearRate <= 0.03f) {
        heldBombVisual_->SetIsVisible(false);
        return;
    }

    const float holdPulse = std::sin(idleTimer_ * 16.0f) * 0.035f;
    const float throwRate = (throwState_ == ThrowState::LeapThrow) ? SmoothStep01(GetThrowLeapProgress() / kThrowReleaseProgress) : SmoothStep01((progress - 0.72f) / 0.28f);
    const float visualScale = kHeldBombScale * (0.36f + appearRate * 0.64f) * (1.0f + holdPulse + throwRate * 0.08f);
    const float yaw = GetRotation().y;

    heldBombVisual_->SetIsVisible(true);
    heldBombVisual_->SetTranslate(ComputeHeldBombPosition(progress));
    heldBombVisual_->SetScale({ visualScale, visualScale, visualScale });
    heldBombVisual_->SetRotation({
        progress * 7.0f + idleTimer_ * 0.7f,
        yaw + progress * 5.0f + throwRate * kPi,
        progress * 1.2f + throwRate * 1.6f
    });
    heldBombVisual_->Update(deltaTime);
}

void EnemyBomber::EnsureHeldBombVisual() {
    if (heldBombVisual_ || !common_) {
        return;
    }

    heldBombVisual_ = std::make_unique<Object3d>();
    heldBombVisual_->Initialize(common_);
    heldBombVisual_->SetModel(kHeldBombModelName);
    heldBombVisual_->SetCollisionAttribute(0);
    heldBombVisual_->SetCollisionMask(0);
    heldBombVisual_->SetScale({ kHeldBombScale, kHeldBombScale, kHeldBombScale });
    heldBombVisual_->SetIsVisible(false);
}

void EnemyBomber::HideHeldBombVisual() {
    if (heldBombVisual_) {
        heldBombVisual_->SetIsVisible(false);
    }
}

Vector3 EnemyBomber::ComputeHeldBombPosition(float progress) const {
    progress = Saturate(progress);
    const float yaw = GetRotation().y - kBomberModelYawOffset;
    const Vector3 forward = { std::sin(yaw), 0.0f, std::cos(yaw) };
    const Vector3 right = { std::cos(yaw), 0.0f, -std::sin(yaw) };
    const float extractRate = SmoothStep01(progress / 0.22f);
    const float liftRate = SmoothStep01((progress - 0.08f) / 0.32f);
    const float holdBackRate = SmoothStep01((progress - 0.40f) / 0.24f);
    const float throwRate = SmoothStep01((progress - 0.74f) / 0.26f);

    Vector3 position = GetTranslate();
    const float forwardOffset = 1.25f + extractRate * 0.45f - holdBackRate * 0.25f + throwRate * 1.45f;
    const float sideOffset = std::sin(progress * kPi) * 0.16f * footworkDirection_;
    const float heightOffset = 1.45f + liftRate * 1.35f - holdBackRate * 0.12f + throwRate * 0.18f;
    position.x += forward.x * forwardOffset + right.x * sideOffset;
    position.y += heightOffset;
    position.z += forward.z * forwardOffset + right.z * sideOffset;
    return position;
}

float EnemyBomber::GetThrowProgress() const {
    return 1.0f - std::clamp(windupTimer_ / (std::max)(throwWindup_, 0.01f), 0.0f, 1.0f);
}

float EnemyBomber::GetThrowLeapProgress() const {
    return 1.0f - std::clamp(throwLeapTimer_ / kThrowLeapDuration, 0.0f, 1.0f);
}

void EnemyBomber::UpdateFootwork(float deltaTime, const Vector3& direction, float distance, bool inRange) {
    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    if (inRange) {
        footworkTimer_ -= deltaTime;
        if (footworkTimer_ <= 0.0f) {
            footworkDirection_ *= -1.0f;
            footworkTimer_ = 1.4f;
        }

        Vector3 side = { direction.z * footworkDirection_, 0.0f, -direction.x * footworkDirection_ };
        velocity.x += side.x * kFootworkSpeed;
        velocity.z += side.z * kFootworkSpeed;

        if (throwRecoverPoseTimer_ > 0.0f) {
            const float recoverRate = Saturate(throwRecoverPoseTimer_ / kThrowRecoverPoseDuration);
            velocity.x -= direction.x * recoverRate * 1.15f;
            velocity.z -= direction.z * recoverRate * 1.15f;
            velocity.x += side.x * recoverRate * 0.45f;
            velocity.z += side.z * recoverRate * 0.45f;
        }

        if (distance < kPreferredDistance) {
            velocity.x -= direction.x * 0.8f;
            velocity.z -= direction.z * 0.8f;
        } else if (distance > kPreferredDistance + 4.0f) {
            velocity.x += direction.x * 0.6f;
            velocity.z += direction.z * 0.6f;
        }
    } else {
        velocity = CalculateWanderVelocity(deltaTime, kFootworkSpeed * 0.65f, 0.7f);
        Vector3 wanderDirection = { velocity.x, 0.0f, velocity.z };
        const float lengthSq = wanderDirection.x * wanderDirection.x + wanderDirection.z * wanderDirection.z;
        if (lengthSq > 0.0001f) {
            const float targetYaw = std::atan2(wanderDirection.x, wanderDirection.z) + kBomberModelYawOffset;
            SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.08f));
        }
    }

    SetVelocity(velocity);
}

void EnemyBomber::BeginThrow() {
    throwState_ = ThrowState::Windup;
    windupTimer_ = throwWindup_;
    throwLeapTimer_ = 0.0f;
    throwLandingTimer_ = 0.0f;
    throwBombReleased_ = false;
    EnsureHeldBombVisual();
    UpdateHeldBombVisual(0.0f);
}

void EnemyBomber::BeginThrowLeap() {
    throwState_ = ThrowState::LeapThrow;
    throwLeapTimer_ = kThrowLeapDuration;
    throwLandingTimer_ = 0.0f;
    throwBombReleased_ = false;
    throwLeapBaseYaw_ = GetRotation().y;
    const float throwMoveYaw = throwLeapBaseYaw_ - kBomberModelYawOffset;
    throwSpinDirection_ = (footworkDirection_ >= 0.0f) ? 1.0f : -1.0f;

    Vector3 velocity = GetVelocity();
    velocity.y = (std::max)(velocity.y, kThrowJumpVelocity);
    velocity.x = std::sin(throwMoveYaw) * kThrowHopForwardSpeed;
    velocity.z = std::cos(throwMoveYaw) * kThrowHopForwardSpeed;
    SetVelocity(velocity);
    Vector3 position = GetTranslate();
    position.y += kThrowTakeoffLift;
    SetTranslate(position);
    isGrounded_ = false;
    UpdateHeldBombVisual(0.0f);
}

void EnemyBomber::UpdateThrowLeap(float deltaTime) {
    throwLeapTimer_ -= deltaTime;
    if (!throwBombReleased_) {
        UpdateHeldBombVisual(deltaTime);
    }

    const float progress = GetThrowLeapProgress();
    if (!throwBombReleased_ && progress >= kThrowReleaseProgress) {
        ThrowBomb();
        throwBombReleased_ = true;
    }

    if (throwBombReleased_ && ((isGrounded_ && progress >= 0.68f) || throwLeapTimer_ <= -0.32f)) {
        BeginThrowLanding();
    }
}

void EnemyBomber::BeginThrowLanding() {
    throwState_ = ThrowState::Landing;
    throwLandingTimer_ = kThrowLandingDuration;
    throwLeapTimer_ = 0.0f;
    throwRecoverPoseTimer_ = kThrowRecoverPoseDuration;
    HideHeldBombVisual();
    HideAttackTelegraph();
}

void EnemyBomber::ThrowBomb() {
    HideAttackTelegraph();
    HideHeldBombVisual();
    throwRecoilTimer_ = kThrowRecoilDuration;
    throwRecoverPoseTimer_ = 0.0f;
    if (!spawnCallback_ || !common_ || !target_) return;

    const Vector3 bodyPos = GetTranslate();
    Vector3 targetDirection = target_->GetTranslate() - bodyPos;
    targetDirection.y = 0.0f;
    float directionLength = std::sqrt(targetDirection.x * targetDirection.x + targetDirection.z * targetDirection.z);
    if (directionLength > 0.001f) {
        targetDirection.x /= directionLength;
        targetDirection.z /= directionLength;
    } else {
        targetDirection = { std::sin(GetRotation().y), 0.0f, std::cos(GetRotation().y) };
    }

    const Vector3 releasePos = {
        bodyPos.x + targetDirection.x * 2.95f,
        bodyPos.y + 2.85f,
        bodyPos.z + targetDirection.z * 2.95f
    };

    auto bomb = EnemyFactory::GetInstance()->CreateEnemy("Bomb", common_);
    if (!bomb) return;

    Vector3 spawnPos = releasePos;
    bomb->SetTranslate(spawnPos);
    bomb->SetTarget(target_);

    Vector3 toTarget = target_->GetTranslate() - spawnPos;
    toTarget.y = 0.0f;
    float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (distance > 0.001f) {
        toTarget.x /= distance;
        toTarget.z /= distance;
    } else {
        toTarget = targetDirection;
        distance = directionLength;
    }

    bomb->SetRotationY(std::atan2(toTarget.x, toTarget.z));
    bomb->SetCarried(false);

    const Vector3 targetPos = target_->GetTranslate();
    Vector3 deltaToTarget = {
        targetPos.x - spawnPos.x,
        (targetPos.y + 0.35f) - spawnPos.y,
        targetPos.z - spawnPos.z
    };
    const float horizontalDistance = std::sqrt(deltaToTarget.x * deltaToTarget.x + deltaToTarget.z * deltaToTarget.z);
    const float flightTime = std::clamp(horizontalDistance / 12.0f, 0.85f, 1.35f);
    const float gravity = 60.0f;
    const float velocityX = deltaToTarget.x / flightTime;
    const float velocityZ = deltaToTarget.z / flightTime;
    float velocityY = (deltaToTarget.y + 0.5f * gravity * flightTime * flightTime) / flightTime;
    velocityY = std::clamp(velocityY, 14.0f, 28.0f);
    bomb->SetVelocity({ velocityX, velocityY, velocityZ });

    if (auto* enemyBomb = dynamic_cast<EnemyBomb*>(bomb.get())) {
        enemyBomb->Ignite(2.8f);
    }

    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            kCarryBomberThrowEffect,
            releasePos,
            { 0.0f, std::atan2(toTarget.x, toTarget.z), 0.0f },
            { 0.65f, 0.65f, 0.65f }
        );
    }
    if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
        gpuParticleManager->Emit(kCarryBomberSparkPreset, releasePos);
    }

    spawnCallback_(std::move(bomb));
}

void EnemyBomber::ThrowCarryBomb(Player* player) {
    if (!player || !common_) {
        return;
    }

    auto bomb = EnemyFactory::GetInstance()->CreateEnemy("Bomb", common_);
    if (!bomb) {
        return;
    }

    const Vector3 forward = GetPlayerForward(player);
    const Vector3 playerPos = player->GetWorldPosition();
    Vector3 spawnPos = {
        playerPos.x + forward.x * 2.1f,
        playerPos.y + 2.15f,
        playerPos.z + forward.z * 2.1f
    };

    bomb->SetTranslate(spawnPos);
    bomb->SetRotationY(std::atan2(forward.x, forward.z));
    bomb->SetTarget(target_ ? target_ : player);
    bomb->SetCarried(false);
    bomb->SetVelocity({
        forward.x * kCarryBombForwardSpeed,
        kCarryBombUpSpeed,
        forward.z * kCarryBombForwardSpeed
    });

    if (auto* enemyBomb = dynamic_cast<EnemyBomb*>(bomb.get())) {
        enemyBomb->Ignite(2.25f);
    }
    throwRecoilTimer_ = kThrowRecoilDuration;
    throwRecoverPoseTimer_ = kThrowRecoverPoseDuration;

    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            kCarryBomberThrowEffect,
            spawnPos,
            { 0.0f, std::atan2(forward.x, forward.z), 0.0f },
            { 1.0f, 1.0f, 1.0f }
        );
    }
    if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
        gpuParticleManager->Emit(kCarryBomberSparkPreset, spawnPos);
    }

    SpawnBombObject(std::move(bomb));
}

void EnemyBomber::SpawnBombObject(std::unique_ptr<BaseEnemy> bomb) {
    if (!bomb) {
        return;
    }

    if (spawnCallback_) {
        spawnCallback_(std::move(bomb));
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->GetCurrentScene()) {
        return;
    }

    if (!bomb->IsCarried()) {
        bomb->SetTarget(sceneManager->GetCurrentScene()->GetPlayer());
    }
    sceneManager->GetCurrentScene()->AddObject(std::move(bomb));
}

Vector3 EnemyBomber::GetPlayerForward(Player* player) const {
    Vector3 forward = player->GetForwardDirection();
    const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { forward.x / length, 0.0f, forward.z / length };
}









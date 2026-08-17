#include "EnemyWindSlime.h"

#include "BaseScene.h"
#include "BulletManager.h"
#include "Character.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "GPUParticleManager.h"
#include "GroundEffectLocator.h"
#include "MeshEffectManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "SlimeBounceAnimator.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
constexpr const char* kGustAttackId = "gust_breath";
constexpr const char* kVolleyAttackId = "aerial_wind_volley";
constexpr int kWindOrbMaterialType = 26;
constexpr float kWindSlimeModelYawOffset = 3.1415926535f;
constexpr float kGroundCollisionWorldRadius = 0.82f;
constexpr float kThrownCollisionWorldRadius = 1.18f;
constexpr float kMoveHopInterval = 0.24f;
constexpr float kMoveHopPower = 5.25f;
constexpr float kUpdraftCooldown = 1.85f;
constexpr float kDashCooldown = 0.62f;
constexpr float kDashDistance = 6.6f;
constexpr float kDashMinimumDistance = 1.0f;
constexpr float kDashInvincibleDuration = 0.14f;
constexpr float kUpdraftRadius = 3.8f;
constexpr float kUpdraftLiftSpeed = 18.0f;
constexpr float kCarriedBreathRefreshTime = 0.16f;
constexpr float kBreathPushInterval = 0.13f;
constexpr float kBreathParticleInterval = 0.045f;
constexpr float kVolleyShotInterval = 0.34f;
constexpr float kVolleyFirstShotDelay = 0.52f;
constexpr float kVolleyPreferredHoverHeight = 3.10f;
constexpr float kVolleyCeilingClearance = 2.25f;
constexpr float kVolleyOrbVisualScale = 0.46f;
constexpr int kVolleyOrbCount = 3;
constexpr float kVolleyImpactLateralSpacing = 1.05f;
constexpr float kVolleyImpactForwardSpacing = 0.38f;
constexpr const char* kIdlePreset = "wind_slime_idle_wisp";
constexpr const char* kChargePreset = "wind_slime_charge";
constexpr const char* kBreathStreamPreset = "wind_slime_breath_stream";
constexpr const char* kBreathDustPreset = "wind_slime_breath_dust";
constexpr const char* kBurstPreset = "wind_slime_gust_burst";
constexpr const char* kImpactPreset = "wind_slime_gust_impact";
constexpr const char* kOrbHoldPreset = "wind_slime_orb_hold";
constexpr const char* kOrbTrailPreset = "wind_slime_orb_trail";
constexpr const char* kOrbImpactPreset = "wind_slime_orb_impact";
constexpr const char* kUpdraftPreset = "player_wind_updraft";
constexpr const char* kDashPreset = "player_wind_dash";
constexpr const char* kBurstRingEffectPath = "Resources/json/effect/effect_wind_gust_ring.json";
constexpr const char* kDashTrailEffectPath = "Resources/json/effect/effect_wind_dash_trail.json";

float SmoothStep01(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

BulletVisualConfig MakeWindOrbVisual() {
    BulletVisualConfig visual;
    visual.materialType = kWindOrbMaterialType;
    visual.blendMode = BlendMode::kNormal;
    visual.color = { 0.42f, 1.0f, 0.80f, 0.96f };
    visual.emissive = 1.9f;
    visual.visualScale = 0.82f;
    visual.waveSpeed = 2.65f;
    visual.waveHeight = 0.72f;
    visual.waveFrequency = 12.0f;
    visual.effectType = 1.0f;
    visual.effectScale = 0.86f;
    visual.effectSoftness = 0.32f;
    visual.effectIntensity = 1.72f;
    visual.billboardScale = 1.0f;
    visual.trailPreset = kOrbTrailPreset;
    visual.impactPreset = kOrbImpactPreset;
    visual.trailInterval = 0.055f;
    visual.trailSpeedScale = 1.25f;
    return visual;
}

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

Object3d* FindEnemyRoot(Object3d* object) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        if (dynamic_cast<BaseEnemy*>(current)) {
            return current;
        }
    }
    return nullptr;
}
}

void EnemyWindSlime::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_WindSlime");
    SetEnemyType("WindSlime");
    ReloadAttackProfile();
    SetColor({ 0.78f, 1.0f, 0.92f, 1.0f });
    defaultColor_ = GetColor();
    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SyncGroundCollisionRadius();
}

void EnemyWindSlime::Update(float deltaTime) {
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    EnsureBaseScale();
    idleTimer_ += deltaTime;
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);
    ambientParticleTimer_ -= deltaTime;
    volleyRecoilTimer_ = (std::max)(0.0f, volleyRecoilTimer_ - deltaTime);

    if (ambientParticleTimer_ <= 0.0f) {
        EmitDirectedWindPreset(kIdlePreset, GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f },
            { 0.15f, 1.0f, 0.08f }, 0.55f);
        ambientParticleTimer_ = 0.18f;
    }

    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;
    UpdateWildBehavior(deltaTime, velocity);

    if (attackState_ == AttackState::Idle &&
        SlimeBounceAnimator::StepGroundHop(groundHopTimer_, velocity, isGrounded_, deltaTime, kMoveHopInterval, 0.10f)) {
        velocity.y = (std::max)(velocity.y, kMoveHopPower);
    }
    SetVelocity(velocity);
    ApplySlimeAnimation(deltaTime);
    SyncGroundCollisionRadius();
    BaseEnemy::Update(deltaTime);
}

bool EnemyWindSlime::HasOwnedSpecialMaterialVisuals() const {
    for (const auto& orb : heldWindOrbVisuals_) {
        if (orb && orb->GetIsVisible()) {
            return true;
        }
    }
    return false;
}

void EnemyWindSlime::DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    for (const auto& orb : heldWindOrbVisuals_) {
        if (orb && orb->GetIsVisible()) {
            orb->DrawWindOrb(depthSrvHandle, grabSrvHandle);
        }
    }
}

void EnemyWindSlime::SetCarried(bool isCarried) {
    BaseEnemy::SetCarried(isCarried);
    if (!isCarried) {
        ReleaseCarriedAbilityVisuals();
    }
}

bool EnemyWindSlime::UpdateInactiveState(float deltaTime) {
    if (isDead || !GetIsVisible()) {
        HideAttackTelegraph();
        HideHeldWindOrbs();
        attackState_ = AttackState::Idle;
        attackStateTimer_ = 0.0f;
        SetVelocity({ 0.0f, 0.0f, 0.0f });
        return true;
    }
    if (ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        HideHeldWindOrbs();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    if (isCarried_) {
        HideAttackTelegraph();
        HideHeldWindOrbs();
        return true;
    }
    return UpdateThrowRecoveryState(deltaTime);
}

bool EnemyWindSlime::UpdateThrowRecoveryState(float deltaTime) {
    if (!IsThrowRecovering()) {
        return false;
    }
    if (IsThrownPhysics()) {
        SyncThrownCollisionRadius();
    } else {
        SyncGroundCollisionRadius();
    }
    BaseEnemy::Update(deltaTime);
    return true;
}

void EnemyWindSlime::EnsureBaseScale() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyWindSlime::UpdateWildBehavior(float deltaTime, Vector3& velocity) {
    Vector3 direction = lockedAttackDirection_;
    float distance = lockedTargetDistance_;
    if (target_ && param_.has_value()) {
        const Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
        direction = NormalizePlanar(toTarget);
        distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    }

    if (attackState_ != AttackState::Idle) {
        UpdateAttackState(deltaTime, velocity, direction, distance);
        return;
    }
    if (!target_ || !param_.has_value()) {
        const float speed = param_.has_value() ? (std::max)(0.55f, param_->speed * 0.45f) : 0.8f;
        velocity = CalculateWanderVelocity(deltaTime, speed, 0.72f);
        UpdateFacing(velocity);
        return;
    }

    if (UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
        UpdateFacing(direction);
        return;
    }

    const EnemyAttackDefinition& gust = GetAttackDefinition(kGustAttackId);
    const EnemyAttackDefinition& volley = GetAttackDefinition(kVolleyAttackId);
    const bool allowGust = debugPreviewAttackId_.empty() || debugPreviewAttackId_ == kGustAttackId;
    const bool allowVolley = debugPreviewAttackId_.empty() || debugPreviewAttackId_ == kVolleyAttackId;
    UpdateFacing(direction);

    if (allowGust && distance >= gust.minRange && distance <= gust.maxRange && attackCooldown_ <= 0.0f) {
        StartGustBreath(direction);
    }
    else if (allowVolley && distance >= volley.minRange && distance <= volley.maxRange && attackCooldown_ <= 0.0f) {
        StartAerialWindVolley(direction, distance);
    }
    else if (distance <= detectionRange_) {
        const float speed = (std::max)(0.0f, param_->speed) * 1.12f;
        const float approach = distance > gust.maxRange * 0.78f ? 1.0f : -0.46f;
        velocity.x = direction.x * speed * approach;
        velocity.z = direction.z * speed * approach;
    }
    else {
        velocity = CalculateWanderVelocity(deltaTime, (std::max)(0.55f, param_->speed * 0.48f), 0.72f);
        UpdateFacing(velocity);
    }
}

void EnemyWindSlime::UpdateAttackState(
    float deltaTime,
    Vector3& velocity,
    const Vector3& direction,
    float distance) {
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    if (attackState_ == AttackState::Recover) {
        HideAttackTelegraph();
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        if (attackStateTimer_ <= 0.0f) {
            attackState_ = AttackState::Idle;
        }
        return;
    }

    if (attackState_ == AttackState::GustActive) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        breathParticleTimer_ -= deltaTime;
        breathPushTimer_ -= deltaTime;
        const Vector3 origin = GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f };
        const float pushProgress = breathPushTimer_ <= 0.0f
            ? 1.0f
            : 1.0f - std::clamp(breathPushTimer_ / kBreathPushInterval, 0.0f, 1.0f);

        ShowAttackTelegraphCone(GetWorldPosition(), lockedAttackDirection_, attack.maxRange,
            0.72f, attack.radius * 2.0f, pushProgress, { 0.46f, 1.0f, 0.84f, 0.72f });
        if (breathParticleTimer_ <= 0.0f) {
            EmitWindBreathParticles(origin, lockedAttackDirection_, attack.maxRange);
            breathParticleTimer_ += kBreathParticleInterval;
        }
        if (breathPushTimer_ <= 0.0f) {
            DispatchEnemyBreathPush(lockedAttackDirection_, distance);
            breathPushTimer_ += kBreathPushInterval;
        }
        if (attackStateTimer_ <= 0.0f) {
            BeginRecover(attack);
        }
        return;
    }

    if (attackState_ == AttackState::VolleyTakeoff) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        const float duration = (std::max)(0.01f, attack.windupDuration);
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        breathParticleTimer_ -= deltaTime;
        const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);

        if (progress < 0.64f) {
            lockedAttackDirection_ = direction;
            lockedTargetDistance_ = distance;
            UpdateVolleyImpactCenters();
        }
        UpdateFacing(lockedAttackDirection_);
        const float height = volleyGroundY_ + (volleyHoverY_ - volleyGroundY_) * SmoothStep01(progress);
        HoldVolleyAltitude(height, deltaTime, velocity);
        ShowVolleyImpactTelegraphs(progress);
        UpdateHeldWindOrbs(deltaTime, SmoothStep01((progress - 0.38f) / 0.52f));

        if (breathParticleTimer_ <= 0.0f) {
            const Vector3 liftOrigin = { GetWorldPosition().x, volleyGroundY_ + 0.12f, GetWorldPosition().z };
            EmitDirectedWindPreset(kUpdraftPreset, liftOrigin,
                { -lockedAttackDirection_.x * 0.12f, 1.0f, -lockedAttackDirection_.z * 0.12f },
                1.0f + progress * 0.72f);
            EmitDirectedWindPreset(kChargePreset, GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f },
                { 0.0f, 1.0f, 0.0f }, 0.88f + progress * 0.64f);
            breathParticleTimer_ += 0.065f;
        }
        if (!warningTriggered_ && attackStateTimer_ <= attack.warningLeadTime) {
            TriggerAttackTelegraphCue({ 0.72f, 1.0f, 0.92f, 1.0f });
            warningTriggered_ = true;
        }
        if (attackStateTimer_ <= 0.0f) {
            attackState_ = AttackState::VolleyActive;
            const float minimumActive = kVolleyFirstShotDelay + kVolleyShotInterval * 2.0f + 0.28f;
            attackStateTimer_ = (std::max)(minimumActive, attack.activeDuration);
            volleyShotTimer_ = kVolleyFirstShotDelay;
            volleyShotCount_ = 0;
            volleyVisualTimer_ = 0.0f;
            EmitWindPreset(kBurstPreset, GetWorldPosition() + Vector3{ 0.0f, 0.82f, 0.0f });
        }
        return;
    }

    if (attackState_ == AttackState::VolleyActive) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        volleyShotTimer_ -= deltaTime;
        const float hoverBob = std::sin(idleTimer_ * 5.8f) * 0.10f;
        HoldVolleyAltitude(volleyHoverY_ + hoverBob, deltaTime, velocity);
        UpdateFacing(direction);
        lockedAttackDirection_ = direction;
        lockedTargetDistance_ = distance;
        if (volleyShotCount_ < kVolleyOrbCount) {
            const float shotInterval = volleyShotCount_ == 0
                ? (std::max)(kVolleyFirstShotDelay, 0.01f)
                : (std::max)(kVolleyShotInterval, 0.01f);
            const float shotProgress = 1.0f - std::clamp(volleyShotTimer_ / shotInterval, 0.0f, 1.0f);
            ShowVolleyImpactTelegraphs(shotProgress);
        } else {
            HideAttackTelegraph();
        }
        UpdateHeldWindOrbs(deltaTime);

        while (volleyShotCount_ < kVolleyOrbCount && volleyShotTimer_ <= 0.0f) {
            FireAerialWindOrb(volleyShotCount_);
            ++volleyShotCount_;
            volleyRecoilTimer_ = 0.18f;
            volleyShotTimer_ += kVolleyShotInterval;
        }
        if (attackStateTimer_ <= 0.0f && volleyShotCount_ >= kVolleyOrbCount) {
            BeginVolleyLanding(attack);
        }
        return;
    }

    if (attackState_ == AttackState::VolleyLanding) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        const float duration = (std::max)(0.16f, attack.recoveryDuration);
        attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
        const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);
        const float height = volleyLandingStartY_ + (volleyGroundY_ - volleyLandingStartY_) * SmoothStep01(progress);
        HoldVolleyAltitude(height, deltaTime, velocity);
        HideHeldWindOrbs();
        breathParticleTimer_ -= deltaTime;
        if (breathParticleTimer_ <= 0.0f) {
            EmitDirectedWindPreset(kUpdraftPreset, GetWorldPosition(), { 0.0f, -1.0f, 0.0f }, 0.78f);
            breathParticleTimer_ += 0.09f;
        }
        if (attackStateTimer_ <= 0.0f) {
            Vector3 position = GetTranslate();
            position.y = volleyGroundY_;
            SetTranslate(position);
            velocity.y = 0.0f;
            SetGrounded(true);
            EmitWindPreset(kImpactPreset, position + Vector3{ 0.0f, 0.16f, 0.0f });
            attackState_ = AttackState::Recover;
            recoveryDuration_ = 0.18f;
            attackStateTimer_ = recoveryDuration_;
        }
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
    const float duration = (std::max)(0.01f, attack.windupDuration);
    attackStateTimer_ = (std::max)(0.0f, attackStateTimer_ - deltaTime);
    breathParticleTimer_ -= deltaTime;
    const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);

    if (progress < 0.58f) {
        lockedAttackDirection_ = direction;
        lockedTargetDistance_ = distance;
        UpdateFacing(direction);
    }
    ShowAttackTelegraphCone(GetWorldPosition(), lockedAttackDirection_, attack.maxRange,
        0.72f, attack.radius * 2.0f, progress, { 0.42f, 1.0f, 0.86f, 0.82f });
    if (!warningTriggered_ && attackStateTimer_ <= attack.warningLeadTime) {
        TriggerAttackTelegraphCue({ 0.70f, 1.0f, 0.94f, 1.0f });
        warningTriggered_ = true;
    }
    if (breathParticleTimer_ <= 0.0f) {
        EmitDirectedWindPreset(kChargePreset, GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f },
            lockedAttackDirection_ * -1.0f + Vector3{ 0.0f, 0.18f, 0.0f }, 0.82f + progress * 0.75f);
        breathParticleTimer_ += 0.07f;
    }

    if (attackStateTimer_ <= 0.0f) {
        attackState_ = AttackState::GustActive;
        attackStateTimer_ = (std::max)(0.18f, attack.activeDuration);
        breathParticleTimer_ = 0.0f;
        breathPushTimer_ = 0.0f;
        breathParticleCursor_ = 0;
        EmitWindPreset(kBurstPreset, GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f });
    }
}

void EnemyWindSlime::StartGustBreath(const Vector3& direction) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
    attackState_ = AttackState::GustWindup;
    attackStateTimer_ = (std::max)(0.01f, attack.windupDuration);
    lockedAttackDirection_ = NormalizePlanar(direction);
    warningTriggered_ = false;
    breathParticleTimer_ = 0.0f;
}

void EnemyWindSlime::StartAerialWindVolley(const Vector3& direction, float distance) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
    attackState_ = AttackState::VolleyTakeoff;
    attackStateTimer_ = (std::max)(0.01f, attack.windupDuration);
    lockedAttackDirection_ = NormalizePlanar(direction);
    lockedTargetDistance_ = distance;
    volleyGroundY_ = GetTranslate().y;
    volleyHoverY_ = ResolveVolleyHoverY();
    volleyLandingStartY_ = volleyHoverY_;
    volleyShotTimer_ = 0.0f;
    volleyVisualTimer_ = 0.0f;
    volleyRecoilTimer_ = 0.0f;
    volleyShotCount_ = 0;
    warningTriggered_ = false;
    breathParticleTimer_ = 0.0f;
    UpdateVolleyImpactCenters();
    SetGrounded(false);
    EnsureHeldWindOrbs();
    HideHeldWindOrbs();
}

void EnemyWindSlime::BeginRecover(const EnemyAttackDefinition& attack) {
    attackState_ = AttackState::Recover;
    recoveryDuration_ = (std::max)(0.05f, attack.recoveryDuration);
    attackStateTimer_ = recoveryDuration_;
    attackCooldown_ = attack.cooldown;
    HideAttackTelegraph();
}

void EnemyWindSlime::BeginVolleyLanding(const EnemyAttackDefinition& attack) {
    attackState_ = AttackState::VolleyLanding;
    attackStateTimer_ = (std::max)(0.16f, attack.recoveryDuration);
    recoveryDuration_ = attackStateTimer_;
    attackCooldown_ = attack.cooldown;
    volleyLandingStartY_ = GetTranslate().y;
    volleyVisualTimer_ = 0.0f;
    breathParticleTimer_ = 0.0f;
    HideAttackTelegraph();
    HideHeldWindOrbs();
}

void EnemyWindSlime::FireAerialWindOrb(int orbIndex) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
    const Vector3 spawnPosition = ComputeHeldWindOrbPosition(orbIndex);
    Vector3 aim = lockedAttackDirection_ + Vector3{ 0.0f, -0.08f, 0.0f };
    float targetDistance = lockedTargetDistance_;
    if (orbIndex >= 0 && orbIndex < static_cast<int>(volleyImpactCenters_.size())) {
        // 地上の予告円と実際の弾着点を一致させます。
        aim = volleyImpactCenters_[orbIndex] + Vector3{ 0.0f, 0.24f, 0.0f } - spawnPosition;
        targetDistance = Math::Length(aim);
    }
    if (Math::Length(aim) <= 0.001f) {
        aim = { lockedAttackDirection_.x, -0.08f, lockedAttackDirection_.z };
    }
    aim = Math::Normalize(aim);

    const float minimumSpeed = attack.minSpeed > 0.0f ? attack.minSpeed : 14.0f;
    const float maximumSpeed = attack.maxSpeed > minimumSpeed ? attack.maxSpeed : 22.0f;
    const float speed = std::clamp(targetDistance * 1.65f, minimumSpeed, maximumSpeed);
    const float lifetime = attack.lifetime > 0.0f ? attack.lifetime : 2.8f;
    const float radius = (std::max)(0.32f, attack.radius);
    BulletManager::GetInstance()->Fire(
        spawnPosition,
        aim * speed,
        kEnemyAttack,
        kPlayer | kAllSolid,
        "Primitives/sphere",
        radius,
        lifetime,
        MakeWindOrbVisual(),
        attack.damage,
        {},
        DamageType::Physical);

    EmitDirectedWindPreset(kBurstPreset, spawnPosition, aim, 1.12f);
    if (orbIndex >= 0 && orbIndex < static_cast<int>(heldWindOrbVisuals_.size()) && heldWindOrbVisuals_[orbIndex]) {
        heldWindOrbVisuals_[orbIndex]->SetIsVisible(false);
    }
}

void EnemyWindSlime::UpdateVolleyImpactCenters() {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
    const Vector3 forward = NormalizePlanar(lockedAttackDirection_);
    const Vector3 right = { forward.z, 0.0f, -forward.x };

    Vector3 targetPosition = GetWorldPosition() + forward * std::clamp(
        lockedTargetDistance_,
        attack.minRange,
        attack.maxRange);
    Vector3 targetVelocity = {};
    if (const Character* targetCharacter = dynamic_cast<const Character*>(target_)) {
        targetPosition = targetCharacter->GetWorldPosition();
        targetVelocity = targetCharacter->GetVelocity();
        targetVelocity.y = 0.0f;

        const float speed = Math::Length(targetVelocity);
        constexpr float kMaximumPredictionSpeed = 6.0f;
        if (speed > kMaximumPredictionSpeed && speed > 0.001f) {
            targetVelocity = targetVelocity * (kMaximumPredictionSpeed / speed);
        }
    }

    for (int index = 0; index < kVolleyOrbCount; ++index) {
        const float centeredIndex = static_cast<float>(index - 1);
        const float leadTime = kVolleyFirstShotDelay + kVolleyShotInterval * static_cast<float>(index);
        Vector3 impact = targetPosition + targetVelocity * (leadTime * 0.42f);
        impact += right * (centeredIndex * kVolleyImpactLateralSpacing);
        impact += forward * (-centeredIndex * kVolleyImpactForwardSpacing);
        volleyImpactCenters_[index] = GroundEffectLocator::ResolveGroundPosition(impact);
    }
}

void EnemyWindSlime::ShowVolleyImpactTelegraphs(float progress) {
    if (volleyShotCount_ >= kVolleyOrbCount) {
        HideAttackTelegraph();
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
    const std::size_t firstIndex = static_cast<std::size_t>((std::max)(0, volleyShotCount_));
    ShowAttackTelegraphImpactAreas(
        volleyImpactCenters_.data() + firstIndex,
        volleyImpactCenters_.size() - firstIndex,
        attack.radius * 1.48f,
        progress,
        { 0.48f, 1.0f, 0.82f, 0.80f });
}

float EnemyWindSlime::ResolveVolleyHoverY() {
    constexpr float kProbeHeight = 0.92f;
    const Vector3 origin = GetWorldPosition() + Vector3{ 0.0f, kProbeHeight, 0.0f };
    PhysicsQueryFilter filter;
    filter.mask = kAllSolid;
    filter.ignoredObject = this;
    filter.ignoreDescendants = true;
    filter.includeTriggers = false;
    const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(
        origin,
        0.46f,
        { 0.0f, 1.0f, 0.0f },
        kVolleyPreferredHoverHeight + kVolleyCeilingClearance,
        filter);

    float hoverHeight = kVolleyPreferredHoverHeight;
    if (hit.isHit) {
        const float availableHeight = kProbeHeight + hit.distance - kVolleyCeilingClearance;
        hoverHeight = (std::min)(hoverHeight, (std::max)(0.42f, availableHeight));
    }
    return GetTranslate().y + hoverHeight;
}

void EnemyWindSlime::HoldVolleyAltitude(float height, float deltaTime, Vector3& velocity) {
    Vector3 position = GetTranslate();
    position.y = height;
    SetTranslate(position);
    velocity.y = param_.has_value() ? param_->gravity * deltaTime : 0.0f;
    SetGrounded(false);
}

void EnemyWindSlime::EnsureHeldWindOrbs() {
    if (!common_) {
        return;
    }
    for (int index = 0; index < static_cast<int>(heldWindOrbVisuals_.size()); ++index) {
        if (heldWindOrbVisuals_[index]) {
            continue;
        }
        auto orb = std::make_unique<Object3d>();
        orb->Initialize(common_);
        orb->SetName("WindSlime_HeldOrb_" + std::to_string(index));
        orb->SetModel("Primitives/sphere");
        orb->SetCollisionAttribute(0);
        orb->SetCollisionMask(0);
        orb->SetMaterialType(kWindOrbMaterialType);
        orb->SetBlendMode(BlendMode::kNormal);
        orb->SetColor({ 0.46f, 1.0f, 0.82f, 0.94f });
        orb->SetEmissive(1.8f);
        orb->SetMetallic(0.0f);
        orb->SetRoughness(0.24f);
        if (MeshRenderer* renderer = orb->GetMeshRenderer()) {
            if (auto* param = renderer->GetWaterParamData()) {
                param->waveSpeed = 2.35f;
                param->waveHeight = 0.62f;
                param->waveFrequency = 11.0f;
                param->effectType = static_cast<float>(index) * 0.5f;
                param->effectScale = 0.88f;
                param->effectSoftness = 0.36f;
                param->effectIntensity = 1.58f;
            }
        }
        orb->SetScale({ kVolleyOrbVisualScale, kVolleyOrbVisualScale, kVolleyOrbVisualScale });
        orb->SetIsVisible(false);
        heldWindOrbVisuals_[index] = std::move(orb);
    }
}

void EnemyWindSlime::UpdateHeldWindOrbs(float deltaTime, float appearProgress) {
    EnsureHeldWindOrbs();
    volleyVisualTimer_ -= deltaTime;
    for (int index = 0; index < static_cast<int>(heldWindOrbVisuals_.size()); ++index) {
        Object3d* orb = heldWindOrbVisuals_[index].get();
        if (!orb) {
            continue;
        }
        const float appearThreshold = static_cast<float>(index) / static_cast<float>(kVolleyOrbCount);
        const bool isReady = appearProgress >= appearThreshold && index >= volleyShotCount_;
        orb->SetIsVisible(isReady);
        if (!isReady) {
            continue;
        }

        const float localAppear = SmoothStep01((appearProgress - appearThreshold) * static_cast<float>(kVolleyOrbCount));
        const float pulse = 1.0f + std::sin(idleTimer_ * 9.0f + static_cast<float>(index) * 1.8f) * 0.07f;
        const float scale = kVolleyOrbVisualScale * (0.32f + localAppear * 0.68f) * pulse;
        const Vector3 position = ComputeHeldWindOrbPosition(index);
        orb->SetTranslate(position);
        orb->SetScale({ scale, scale, scale });
        orb->SetRotation({ idleTimer_ * 1.8f, idleTimer_ * 2.7f + static_cast<float>(index), idleTimer_ * 1.2f });
        orb->Update(deltaTime);

        if (volleyVisualTimer_ <= 0.0f) {
            const Vector3 tangent = NormalizePlanar(lockedAttackDirection_ + Vector3{
                static_cast<float>(index - 1) * 0.28f, 0.22f, static_cast<float>(1 - index) * 0.18f });
            EmitDirectedWindPreset(kOrbHoldPreset, position, tangent, 0.88f + static_cast<float>(index) * 0.08f);
        }
    }
    if (volleyVisualTimer_ <= 0.0f) {
        volleyVisualTimer_ = 0.075f;
    }
}

void EnemyWindSlime::HideHeldWindOrbs() {
    for (auto& orb : heldWindOrbVisuals_) {
        if (orb) {
            orb->SetIsVisible(false);
        }
    }
}

Vector3 EnemyWindSlime::ComputeHeldWindOrbPosition(int orbIndex) const {
    const Vector3 direction = NormalizePlanar(lockedAttackDirection_);
    const Vector3 right = { direction.z, 0.0f, -direction.x };
    const float sideOffset = static_cast<float>(orbIndex - 1) * 0.92f;
    const float centerLift = orbIndex == 1 ? 0.18f : 0.0f;
    const float bob = std::sin(idleTimer_ * 5.8f + static_cast<float>(orbIndex) * 1.9f) * 0.10f;
    return GetWorldPosition() + right * sideOffset + direction * 0.18f +
        Vector3{ 0.0f, 1.82f + centerLift + bob, 0.0f };
}

void EnemyWindSlime::DispatchEnemyBreathPush(const Vector3& direction, float distance) {
    const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
    Character* character = dynamic_cast<Character*>(target_);
    if (!character || distance > attack.maxRange + 0.75f) {
        return;
    }

    const Vector3 toTarget = target_->GetWorldPosition() - GetWorldPosition();
    const Vector3 targetDirection = NormalizePlanar(toTarget);
    const float facingDot = Math::Dot(direction, targetDirection);
    if (facingDot < 0.24f) {
        return;
    }

    const float falloff = 1.0f - std::clamp(distance / (attack.maxRange + 0.75f), 0.0f, 1.0f);
    const float planarForce = 22.0f + falloff * 8.0f;
    character->ApplyExternalImpulse(
        { direction.x * planarForce, 6.0f + falloff * 2.5f, direction.z * planarForce }, 0.24f);
    EmitDirectedWindPreset(kImpactPreset, target_->GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f }, direction, 1.15f);
}

void EnemyWindSlime::BeginThrown(const Vector3& initialVelocity) {
    EnsureBaseScale();
    attackState_ = AttackState::Idle;
    attackStateTimer_ = 0.0f;
    warningTriggered_ = false;
    HideAttackTelegraph();
    HideHeldWindOrbs();
    SetScale(baseScale_);
    SyncThrownCollisionRadius();
    BaseEnemy::BeginThrown(initialVelocity);
}

std::unique_ptr<Object3d> EnemyWindSlime::Clone() const {
    auto clone = std::make_unique<EnemyWindSlime>();
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyWindSlime::ExecutePrimaryAbility(Player* player) {
    if (!player || !isCarried_) {
        return;
    }
    if (carriedBreathTimer_ <= 0.0f) {
        carriedBreathParticleTimer_ = 0.0f;
        carriedBreathPushTimer_ = 0.0f;
        breathParticleCursor_ = 0;
        player->TriggerSlimeImpulse({ 1.18f, 0.82f, 1.42f }, 0.14f);
    }
    carriedBreathTimer_ = kCarriedBreathRefreshTime;
}

void EnemyWindSlime::ExecuteAbility(Player* player) {
    if (!player || !isCarried_ || carriedUpdraftCooldown_ > 0.0f) {
        return;
    }
    Vector3 velocity = player->GetVelocity();
    velocity.y = (std::max)(velocity.y, kUpdraftLiftSpeed);
    player->SetVelocity(velocity);
    player->ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Jump, player->GetForwardDirection());
    player->TriggerSlimeImpulse({ 0.74f, 1.62f, 0.74f }, 0.22f);

    const Vector3 center = player->GetWorldPosition();
    DispatchUpdraftPush(player, center);
    for (int index = 0; index < 10; ++index) {
        const float angle = static_cast<float>(index) * 0.62831853f;
        const float radius = 0.28f + static_cast<float>(index) * 0.19f;
        const Vector3 position = center + Vector3{
            std::cos(angle) * radius,
            0.10f + static_cast<float>(index) * 0.18f,
            std::sin(angle) * radius };
        EmitDirectedWindPreset(kUpdraftPreset, position, { -std::sin(angle) * 0.25f, 1.0f, std::cos(angle) * 0.25f },
            1.0f + static_cast<float>(index) * 0.045f);
    }
    if (MeshEffectManager* meshEffects = MeshEffectManager::GetInstance()) {
        meshEffects->SpawnEffectAt(kBurstRingEffectPath, center + Vector3{ 0.0f, 0.06f, 0.0f },
            { 0.0f, idleTimer_, 0.0f }, { kUpdraftRadius, 1.0f, kUpdraftRadius });
    }
    carriedUpdraftCooldown_ = kUpdraftCooldown;
}

void EnemyWindSlime::DispatchUpdraftPush(Player* player, const Vector3& center) {
    PhysicsQueryFilter filter;
    filter.mask = kEnemy;
    filter.ignoredObject = player;
    std::unordered_set<Object3d*> pushed;
    for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(center, kUpdraftRadius, filter)) {
        Object3d* target = FindEnemyRoot(hit.object);
        Character* character = dynamic_cast<Character*>(target);
        if (!character || !pushed.insert(target).second) {
            continue;
        }
        const Vector3 away = NormalizePlanar(target->GetWorldPosition() - center);
        character->ApplyExternalImpulse({ away.x * 5.0f, 16.5f, away.z * 5.0f }, 0.28f);
    }
}

void EnemyWindSlime::ExecuteDashAbility(Player* player) {
    if (!player || !isCarried_ || carriedDashCooldown_ > 0.0f) {
        return;
    }
    Vector3 start{};
    Vector3 destination{};
    Vector3 direction{};
    if (!ResolveDashDestination(player, start, destination, direction)) {
        return;
    }
    SpawnDashEffects(start, destination, direction);
    player->SetTranslate(destination);
    Vector3 velocity = player->GetVelocity();
    velocity.x = direction.x * 15.0f;
    velocity.z = direction.z * 15.0f;
    velocity.y = (std::max)(velocity.y, 2.8f);
    player->SetVelocity(velocity);
    player->SetMoveYaw(std::atan2(direction.x, direction.z));
    player->StartEvasionInvincibility(kDashInvincibleDuration);
    player->ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Dash, direction);
    player->TriggerSlimeImpulse({ 0.76f, 0.88f, 1.62f }, 0.17f);
    player->UpdateLocalMatrix();
    player->UpdateWorldMatrix();
    carriedDashCooldown_ = kDashCooldown;
}

bool EnemyWindSlime::ResolveDashDestination(
    Player* player,
    Vector3& start,
    Vector3& destination,
    Vector3& direction) const {
    start = player->GetWorldPosition();
    Vector3 movement = player->GetVelocity();
    movement.y = 0.0f;
    direction = Math::Length(movement) > 1.2f ? Math::Normalize(movement) : NormalizePlanar(player->GetForwardDirection());

    PhysicsQueryFilter filter;
    filter.mask = kAllSolid;
    filter.ignoredObject = player;
    float distance = kDashDistance;
    const RaycastHit low = CollisionManager::GetInstance()->SphereCast(
        start + Vector3{ 0.0f, 0.72f, 0.0f }, 0.48f, direction, kDashDistance, filter);
    const RaycastHit high = CollisionManager::GetInstance()->SphereCast(
        start + Vector3{ 0.0f, 1.42f, 0.0f }, 0.42f, direction, kDashDistance, filter);
    if (low.isHit) distance = (std::min)(distance, low.distance - 0.30f);
    if (high.isHit) distance = (std::min)(distance, high.distance - 0.30f);
    distance = (std::max)(0.0f, distance);
    if (distance < kDashMinimumDistance) {
        return false;
    }
    destination = start + direction * distance;
    destination.y += 0.12f;
    return true;
}

void EnemyWindSlime::SpawnDashEffects(
    const Vector3& start,
    const Vector3& destination,
    const Vector3& direction) {
    const Vector3 offset = destination - start;
    const float distance = (std::max)(0.001f, Math::Length(Vector3{ offset.x, 0.0f, offset.z }));
    const float yaw = std::atan2(direction.x, direction.z);
    if (MeshEffectManager* meshEffects = MeshEffectManager::GetInstance()) {
        meshEffects->SpawnEffectAt(kDashTrailEffectPath,
            (start + destination) * 0.5f + Vector3{ 0.0f, 0.62f, 0.0f },
            { 0.0f, yaw, 0.0f }, { 0.85f, 1.0f, distance });
    }
    for (int index = 0; index <= 8; ++index) {
        const float t = static_cast<float>(index) / 8.0f;
        EmitDirectedWindPreset(kDashPreset,
            start + offset * t + Vector3{ 0.0f, 0.48f + std::sin(t * 3.14159265f) * 0.18f, 0.0f },
            direction, 1.15f + t * 0.55f);
    }
}

void EnemyWindSlime::UpdateCarriedAbility(Player* player, float deltaTime) {
    if (!isCarried_ || !player) {
        return;
    }
    idleTimer_ += deltaTime;
    carriedUpdraftCooldown_ = (std::max)(0.0f, carriedUpdraftCooldown_ - deltaTime);
    carriedDashCooldown_ = (std::max)(0.0f, carriedDashCooldown_ - deltaTime);
    carriedAuraTimer_ -= deltaTime;

    if (carriedBreathTimer_ > 0.0f) {
        carriedBreathTimer_ = (std::max)(0.0f, carriedBreathTimer_ - deltaTime);
        carriedBreathParticleTimer_ -= deltaTime;
        carriedBreathPushTimer_ -= deltaTime;
        const Vector3 direction = NormalizePlanar(player->GetForwardDirection());
        const Vector3 origin = player->GetWorldPosition() + Vector3{ 0.0f, 0.74f, 0.0f };
        const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
        if (carriedBreathParticleTimer_ <= 0.0f) {
            EmitWindBreathParticles(origin, direction, attack.maxRange);
            carriedBreathParticleTimer_ += kBreathParticleInterval;
        }
        if (carriedBreathPushTimer_ <= 0.0f) {
            DispatchCarriedBreathPush(player, direction);
            carriedBreathPushTimer_ += kBreathPushInterval;
        }
        player->ForceSlimeAnimationModeForNextUpdate(PlayerSlimeAnimator::Mode::Idle, direction);
    }

    if (carriedAuraTimer_ <= 0.0f) {
        EmitDirectedWindPreset(kIdlePreset, player->GetWorldPosition() + Vector3{ 0.0f, 0.65f, 0.0f },
            { 0.12f, 1.0f, 0.04f }, 0.52f);
        carriedAuraTimer_ = 0.18f;
    }
}

void EnemyWindSlime::DispatchCarriedBreathPush(Player* player, const Vector3& direction) {
    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (!scene || !player) {
        return;
    }

    const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
    const Vector3 origin = player->GetWorldPosition();
    for (const auto& object : scene->GetObjects()) {
        BaseEnemy* enemy = dynamic_cast<BaseEnemy*>(object.get());
        if (!enemy || enemy == this || enemy->IsCarried() || enemy->IsDefeatEffectPlaying() ||
            enemy->isDead || !enemy->GetIsVisible()) {
            continue;
        }

        Vector3 toTarget = enemy->GetWorldPosition() - origin;
        toTarget.y = 0.0f;
        const float distance = Math::Length(toTarget);
        if (distance <= 0.001f || distance > attack.maxRange + 0.9f) {
            continue;
        }
        const Vector3 targetDirection = Math::Normalize(toTarget);
        if (Math::Dot(direction, targetDirection) < 0.20f) {
            continue;
        }

        const float falloff = 1.0f - std::clamp(distance / (attack.maxRange + 0.9f), 0.0f, 1.0f);
        const float planarForce = 21.0f + falloff * 10.0f;
        enemy->ApplyExternalImpulse(
            { direction.x * planarForce, 5.5f + falloff * 3.5f, direction.z * planarForce }, 0.24f);
        EmitDirectedWindPreset(kImpactPreset, enemy->GetWorldPosition() + Vector3{ 0.0f, 0.72f, 0.0f },
            direction, 1.0f + falloff * 0.42f);
    }
}

void EnemyWindSlime::ReleaseCarriedAbilityVisuals() {
    carriedBreathTimer_ = 0.0f;
    carriedBreathParticleTimer_ = 0.0f;
    carriedBreathPushTimer_ = 0.0f;
    HideHeldWindOrbs();
}

void EnemyWindSlime::SetDebugPreviewAttackId(const std::string& attackId) {
    debugPreviewAttackId_ = attackId;
    attackCooldown_ = 0.0f;
    attackState_ = AttackState::Idle;
    attackStateTimer_ = 0.0f;
    volleyShotCount_ = 0;
    HideHeldWindOrbs();
}

const char* EnemyWindSlime::GetDebugAttackPhaseName() const {
    switch (attackState_) {
    case AttackState::GustWindup: return "暴風ブレス・溜め";
    case AttackState::GustActive: return "暴風ブレス・吹き飛ばし中";
    case AttackState::VolleyTakeoff: return "三連風弾・風で上昇";
    case AttackState::VolleyActive:
        return volleyShotCount_ == 0 ? "三連風弾・頭上に形成" : "三連風弾・順番に投射";
    case AttackState::VolleyLanding: return "三連風弾・着地";
    case AttackState::Recover: return "攻撃後・反動";
    default: return attackCooldown_ > 0.0f ? "再使用待ち" : "待機・移動";
    }
}

void EnemyWindSlime::ApplyManagedScale(const Vector3& scale) {
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
}

void EnemyWindSlime::UpdateFacing(const Vector3& direction) {
    if (direction.x * direction.x + direction.z * direction.z <= 0.0001f) {
        return;
    }
    const float targetYaw = std::atan2(direction.x, direction.z) + kWindSlimeModelYawOffset;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.18f));
}

void EnemyWindSlime::ApplySlimeAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    Vector3 targetRotation = { 0.0f, GetRotation().y, 0.0f };
    if (attackState_ == AttackState::GustWindup) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kGustAttackId);
        const float duration = (std::max)(0.01f, attack.windupDuration);
        const float charge = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);
        const float flutter = std::sin(charge * 18.8495559f) * 0.035f * charge;
        targetScale = {
            baseScale_.x * (1.0f + charge * 0.28f + flutter),
            baseScale_.y * (1.0f - charge * 0.24f),
            baseScale_.z * (1.0f + charge * 0.18f - flutter),
        };
        targetRotation.z = flutter * 1.4f;
    }
    else if (attackState_ == AttackState::VolleyTakeoff) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        const float duration = (std::max)(0.01f, attack.windupDuration);
        const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);
        const float launchSquash = std::sin(std::clamp(progress / 0.46f, 0.0f, 1.0f) * 3.14159265f) * 0.19f;
        const float airborneStretch = std::sin(progress * 3.14159265f) * 0.17f;
        targetScale = {
            baseScale_.x * (1.0f + launchSquash - airborneStretch * 0.42f),
            baseScale_.y * (1.0f - launchSquash * 0.58f + airborneStretch),
            baseScale_.z * (1.0f + launchSquash - airborneStretch * 0.42f),
        };
        targetRotation.x = -std::sin(progress * 3.14159265f) * 0.10f;
    }
    else if (attackState_ == AttackState::VolleyActive) {
        const float hoverWave = std::sin(idleTimer_ * 6.2f);
        const float recoilRate = std::clamp(volleyRecoilTimer_ / 0.18f, 0.0f, 1.0f);
        const float recoil = std::sin((1.0f - recoilRate) * 3.14159265f) * 0.16f;
        targetScale = {
            baseScale_.x * (1.0f + hoverWave * 0.045f + recoil),
            baseScale_.y * (1.0f - hoverWave * 0.035f - recoil * 0.54f),
            baseScale_.z * (1.0f - hoverWave * 0.025f + recoil * 0.36f),
        };
        targetRotation.x = -recoil * 0.72f;
        targetRotation.z = hoverWave * 0.055f;
    }
    else if (attackState_ == AttackState::VolleyLanding) {
        const EnemyAttackDefinition& attack = GetAttackDefinition(kVolleyAttackId);
        const float duration = (std::max)(0.16f, attack.recoveryDuration);
        const float progress = std::clamp(1.0f - attackStateTimer_ / duration, 0.0f, 1.0f);
        const float fallingStretch = std::sin(progress * 3.14159265f) * 0.22f;
        targetScale = {
            baseScale_.x * (1.0f - fallingStretch * 0.38f),
            baseScale_.y * (1.0f + fallingStretch),
            baseScale_.z * (1.0f - fallingStretch * 0.38f),
        };
        targetRotation.x = 0.08f * progress;
    }
    else if (attackState_ == AttackState::GustActive) {
        const float activeDuration = (std::max)(0.01f, GetAttackDefinition(kGustAttackId).activeDuration);
        const float progress = 1.0f - std::clamp(attackStateTimer_ / activeDuration, 0.0f, 1.0f);
        const float pulse = std::sin(progress * 31.4159265f) * 0.045f;
        targetScale = {
            baseScale_.x * (1.12f + pulse),
            baseScale_.y * (0.78f - pulse * 0.35f),
            baseScale_.z * (1.38f - pulse * 0.60f),
        };
        targetRotation.x = -0.08f;
    }
    else if (attackState_ == AttackState::Recover) {
        const float duration = (std::max)(0.01f, recoveryDuration_ > 0.0f ? recoveryDuration_ : 0.28f);
        const float remaining = std::clamp(attackStateTimer_ / duration, 0.0f, 1.0f);
        const float rebound = std::sin((1.0f - remaining) * 3.14159265f);
        targetScale = {
            baseScale_.x * (1.0f - rebound * 0.09f),
            baseScale_.y * (1.0f + rebound * 0.16f),
            baseScale_.z * (1.0f + rebound * 0.12f),
        };
    }
    else {
        SlimeBounceAnimator::Params params;
        params.speedForFullBounce = 1.9f;
        params.idleAmplitude = 0.09f;
        params.moveAmplitude = 0.31f;
        params.hopFrequency = 11.4f;
        params.horizontalSquash = 0.30f;
        params.verticalStretch = 0.39f;
        params.airborneStretch = 0.40f;
        targetScale = SlimeBounceAnimator::MakeScale(baseScale_, GetVelocity(), idleTimer_, isGrounded_, params);
    }
    ApplyDamageReactionPose(targetScale, targetRotation);
    const float rate = (std::min)(1.0f, deltaTime * 13.0f);
    SetScale(Math::Lerp(GetScale(), targetScale, rate));
    const Vector3 currentRotation = GetRotation();
    SetRotation({
        currentRotation.x + (targetRotation.x - currentRotation.x) * rate,
        currentRotation.y,
        currentRotation.z + (targetRotation.z - currentRotation.z) * rate,
    });
}

void EnemyWindSlime::EmitWindBreathParticles(
    const Vector3& origin,
    const Vector3& direction,
    float range) {
    const float step = (static_cast<float>(breathParticleCursor_ % kWindAnimationPhaseCount) + 0.5f) /
        static_cast<float>(kWindAnimationPhaseCount);
    const Vector3 planarDirection = NormalizePlanar(direction);
    const Vector3 side = { -planarDirection.z, 0.0f, planarDirection.x };
    const float lateral = std::sin(idleTimer_ * 11.0f + step * 15.0f) * (0.08f + step * 0.34f);
    Vector3 position = origin + planarDirection * (0.34f + step * range * 0.86f) + side * lateral;
    position.y += std::sin(idleTimer_ * 8.0f + step * 9.1f) * 0.12f;
    Vector3 streamDirection = planarDirection + Vector3{ 0.0f, 0.06f + step * 0.06f, 0.0f };
    streamDirection = Math::Normalize(streamDirection);
    EmitDirectedWindPreset(kBreathStreamPreset, position, streamDirection, 1.25f + step * 1.05f);

    if (breathParticleCursor_ % 3 == 0) {
        const Vector3 dustPosition = origin + planarDirection * (range * (0.48f + step * 0.42f));
        EmitDirectedWindPreset(kBreathDustPreset, dustPosition, streamDirection, 1.0f + step * 0.65f);
    }
    ++breathParticleCursor_;
}

void EnemyWindSlime::EmitWindPreset(const char* presetName, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->Emit(presetName, position);
    }
}

void EnemyWindSlime::EmitDirectedWindPreset(
    const char* presetName,
    const Vector3& position,
    const Vector3& direction,
    float speedScale) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized() && presetName && presetName[0] != '\0') {
        particles->EmitDirected(presetName, position, direction, speedScale);
    }
}

void EnemyWindSlime::SyncWorldCollisionRadius(float worldRadius) {
    const Vector3 scale = GetScale();
    const float maxScale = (std::max)({ 0.001f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    SetCollisionRadius(worldRadius / maxScale);
}

void EnemyWindSlime::SyncGroundCollisionRadius() {
    SyncWorldCollisionRadius(kGroundCollisionWorldRadius);
}

void EnemyWindSlime::SyncThrownCollisionRadius() {
    SyncWorldCollisionRadius(kThrownCollisionWorldRadius);
}

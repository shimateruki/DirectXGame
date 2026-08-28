#define NOMINMAX
#include "EnemyRingBurner.h"

#include "CollisionConfig.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "HitEffectDirector.h"
#include "MeshEffectManager.h"
#include "Player.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kModelHeight = 1.46f;
constexpr float kTopRadius = 1.05f;
constexpr float kWaveStartRadiusRatio = 1.60f / 11.0f;
constexpr float kWaveDuration = 1.08f;
constexpr float kRecoveryDuration = 0.62f;
constexpr float kSuppressedReleaseDuration = 0.48f;
constexpr float kWaveBandHalfWidth = 0.72f;
constexpr float kWaveJumpClearHeight = 1.52f;
constexpr float kWaveDamage = 1.0f;
constexpr const char* kChargeEffect = "Resources/json/effect/effect_ring_burner_charge.json";
constexpr const char* kWaveEffect = "Resources/json/effect/effect_ring_burner_wave.json";
constexpr const char* kWaveHeatEffect = "Resources/json/effect/effect_ring_burner_wave_heat.json";
constexpr const char* kChargeParticle = "ring_burner_charge_sparks";
constexpr const char* kFireParticle = "ring_burner_discharge_embers";

float EaseOutQuint(float value) {
    const float t = (std::clamp)(value, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse * inverse * inverse;
}

float PlanarDistance(const Vector3& first, const Vector3& second) {
    const float x = first.x - second.x;
    const float z = first.z - second.z;
    return std::sqrt(x * x + z * z);
}

Vector3 NormalizePlanar(const Vector3& value) {
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}
}

void EnemyRingBurner::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_RingBurner");
    SetEnemyType("RingBurner");
    SetStatic(false);
    SetMaterialType(0);
    SetMetallic(0.72f);
    SetRoughness(0.24f);
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    SetEmissive(1.35f);
    defaultColor_ = GetColor();

    // kGroundを併記し、上面だけは安全な足場として押し戻せるようにします。
    SetCollisionAttribute(kEnemy | kGround);
    SetCollisionMask(kPlayer | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kOBB);
    SetCollisionSize({ 1.25f, kModelHeight * 0.5f, 1.25f });
    ColliderConfig collider = GetColliderConfig();
    collider.center = { 0.0f, kModelHeight * 0.5f, 0.0f };
    SetColliderConfig(collider);

    baseScale_ = GetScale();
    hasBaseScale_ = true;
    cooldownTimer_ = 0.18f;
}

void EnemyRingBurner::Update(float deltaTime) {
    ResolveRuntimeTarget();
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    EnsureBaseScale();
    idleTimer_ += deltaTime;
    particleTimer_ = (std::max)(0.0f, particleTimer_ - deltaTime);
    SetVelocity({ 0.0f, 0.0f, 0.0f });

    if (IsTargetStandingOnTop()) {
        if (state_ != State::Suppressed) {
            EnterState(State::Suppressed, kSuppressedReleaseDuration);
            HideAttackTelegraph();
        }
        stateTimer_ = kSuppressedReleaseDuration;
        cooldownTimer_ = GetCooldownDuration() * 0.38f;
    }
    else {
        UpdateCombat(deltaTime);
    }

    UpdateVisualPose(deltaTime);
    BaseEnemy::Update(deltaTime);
}

void EnemyRingBurner::ResolveRuntimeTarget() {
    if (target_) {
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    if (scene) {
        target_ = scene->GetPlayer();
    }
}

bool EnemyRingBurner::UpdateInactiveState(float deltaTime) {
    if (IsDormant() || ShouldHandleDefeatEffect()) {
        HideAttackTelegraph();
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

void EnemyRingBurner::EnsureBaseScale() {
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }
}

void EnemyRingBurner::EnterState(State state, float duration) {
    state_ = state;
    stateDuration_ = (std::max)(duration, 0.001f);
    stateTimer_ = stateDuration_;
    if (state != State::Wave) {
        waveDamageDone_ = false;
    }
}

void EnemyRingBurner::BeginCharge() {
    EnterState(State::Charge, GetChargeDuration());
    waveRadius_ = 0.0f;
    SpawnChargeEffects();
    TriggerAttackTelegraphCue({ 1.0f, 0.20f, 0.025f, 1.0f });
}

void EnemyRingBurner::BeginWave() {
    EnterState(State::Wave, kWaveDuration);
    waveDamageDone_ = false;
    waveRadius_ = GetWaveMaximumRadius() * kWaveStartRadiusRatio;
    HideAttackTelegraph();
    SpawnWaveEffects();
}

void EnemyRingBurner::UpdateCombat(float deltaTime) {
    const float targetDistance = GetPlanarTargetDistance();
    const float configuredRange = param_.has_value() ? param_->detectionRange : detectionRange_;
    const float detectRange = (std::clamp)(configuredRange, 8.0f, 80.0f);

    switch (state_) {
    case State::Idle:
        cooldownTimer_ = (std::max)(0.0f, cooldownTimer_ - deltaTime);
        HideAttackTelegraph();
        if (target_ && targetDistance <= detectRange && cooldownTimer_ <= 0.0f) {
            BeginCharge();
        }
        break;
    case State::Charge: {
        stateTimer_ -= deltaTime;
        const float progress = 1.0f - (std::max)(0.0f, stateTimer_) / stateDuration_;
        const float pulseRadius = Math::Lerp(1.05f, 1.72f, EaseOutQuint(progress));
        ShowAttackTelegraphCircle(
            GetWorldPosition() + Vector3{ 0.0f, 0.06f, 0.0f },
            pulseRadius * (std::max)(baseScale_.x, baseScale_.z),
            progress,
            { 1.0f, 0.20f, 0.025f, 0.92f });
        if (particleTimer_ <= 0.0f) {
            if (auto* particles = GPUParticleManager::GetInstance(); particles->IsInitialized()) {
                particles->Emit(kChargeParticle, GetWorldPosition() + Vector3{ 0.0f, 1.12f * baseScale_.y, 0.0f });
            }
            particleTimer_ = 0.12f;
        }
        if (stateTimer_ <= 0.0f) {
            BeginWave();
        }
        break;
    }
    case State::Wave: {
        stateTimer_ -= deltaTime;
        const float progress = 1.0f - (std::max)(0.0f, stateTimer_) / stateDuration_;
        const float maximumRadius = GetWaveMaximumRadius();
        waveRadius_ = Math::Lerp(maximumRadius * kWaveStartRadiusRatio, maximumRadius, EaseOutQuint(progress));
        UpdateWaveDamage();
        if (stateTimer_ <= 0.0f) {
            EnterState(State::Recovery, kRecoveryDuration);
        }
        break;
    }
    case State::Recovery:
        stateTimer_ -= deltaTime;
        if (stateTimer_ <= 0.0f) {
            EnterState(State::Idle, 0.01f);
            cooldownTimer_ = GetCooldownDuration();
        }
        break;
    case State::Suppressed:
        stateTimer_ -= deltaTime;
        if (stateTimer_ <= 0.0f) {
            EnterState(State::Recovery, kSuppressedReleaseDuration);
        }
        break;
    }
}

void EnemyRingBurner::UpdateWaveDamage() {
    if (waveDamageDone_ || !target_) {
        return;
    }

    const Vector3 origin = GetWorldPosition();
    const Vector3 targetPosition = target_->GetWorldPosition();
    const float targetDistance = PlanarDistance(origin, targetPosition);
    const float targetRelativeHeight = targetPosition.y - origin.y;
    const float horizontalScale = (std::max)(baseScale_.x, baseScale_.z);
    const float bandWidth = kWaveBandHalfWidth * horizontalScale;
    const bool crossesRing = std::abs(targetDistance - waveRadius_) <= bandWidth;
    const bool isLowEnough = targetRelativeHeight <= kWaveJumpClearHeight * baseScale_.y;
    if (!crossesRing || !isLowEnough) {
        return;
    }

    const Vector3 knockbackDirection = NormalizePlanar(targetPosition - origin);
    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = kWaveDamage;
    damageEvent.damageType = DamageType::Fire;
    damageEvent.knockbackVelocity = {
        knockbackDirection.x * 13.0f,
        6.0f,
        knockbackDirection.z * 13.0f };
    EventManager::GetInstance()->Dispatch(damageEvent);
    HitEffectDirector::SpawnEnemyAbilityHit(targetPosition + Vector3{ 0.0f, 0.55f, 0.0f });
    waveDamageDone_ = true;
}

void EnemyRingBurner::UpdateVisualPose(float deltaTime) {
    Vector3 targetScale = baseScale_;
    Vector4 targetColor = defaultColor_;
    float emissive = 1.35f;

    switch (state_) {
    case State::Idle: {
        const float breathe = std::sin(idleTimer_ * 2.8f) * 0.025f;
        targetScale.x *= 1.0f + breathe;
        targetScale.y *= 1.0f - breathe * 0.45f;
        targetScale.z *= 1.0f + breathe;
        emissive = 1.35f + (breathe + 0.025f) * 8.0f;
        break;
    }
    case State::Charge: {
        const float progress = 1.0f - (std::max)(0.0f, stateTimer_) / stateDuration_;
        const float pulse = std::sin(progress * kPi * 10.0f) * (0.03f + progress * 0.07f);
        targetScale.x *= 1.0f + pulse;
        targetScale.y *= 0.94f - progress * 0.10f;
        targetScale.z *= 1.0f + pulse;
        targetColor = { 1.0f, 0.54f + progress * 0.32f, 0.30f, 1.0f };
        emissive = 2.2f + progress * 4.8f;
        break;
    }
    case State::Wave: {
        const float progress = 1.0f - (std::max)(0.0f, stateTimer_) / stateDuration_;
        const float recoil = std::exp(-progress * 8.0f);
        targetScale.x *= 1.0f + recoil * 0.13f;
        targetScale.y *= 0.84f + (1.0f - recoil) * 0.16f;
        targetScale.z *= 1.0f + recoil * 0.13f;
        targetColor = { 1.0f, 0.48f, 0.22f, 1.0f };
        emissive = 4.2f - progress * 2.2f;
        break;
    }
    case State::Recovery:
        targetScale.y *= 0.94f;
        emissive = 1.05f;
        break;
    case State::Suppressed:
        targetScale.x *= 1.05f;
        targetScale.y *= 0.82f;
        targetScale.z *= 1.05f;
        targetColor = { 0.38f, 0.92f, 1.0f, 1.0f };
        emissive = 0.72f;
        break;
    }

    Vector3 currentScale = GetScale();
    currentScale = Math::Lerp(currentScale, targetScale, (std::min)(1.0f, deltaTime * 12.0f));
    SetScale(currentScale);
    SetColor(targetColor);
    SetEmissive(emissive);
}

bool EnemyRingBurner::IsTargetStandingOnTop() const {
    const Player* player = dynamic_cast<const Player*>(target_);
    if (!player || !player->IsGrounded()) {
        return false;
    }

    const Vector3 origin = GetWorldPosition();
    const Vector3 targetPosition = player->GetWorldPosition();
    const float horizontalScale = (std::max)(baseScale_.x, baseScale_.z);
    const float distance = PlanarDistance(origin, targetPosition);
    const float relativeHeight = targetPosition.y - origin.y;
    return distance <= kTopRadius * horizontalScale &&
        relativeHeight >= kModelHeight * baseScale_.y * 0.62f &&
        relativeHeight <= kModelHeight * baseScale_.y + 2.4f;
}

float EnemyRingBurner::GetPlanarTargetDistance() const {
    return target_ ? PlanarDistance(GetWorldPosition(), target_->GetWorldPosition()) : 99999.0f;
}

float EnemyRingBurner::GetChargeDuration() const {
    return param_.has_value() ? (std::clamp)(param_->shakeDuration, 0.45f, 1.40f) : 0.78f;
}

float EnemyRingBurner::GetCooldownDuration() const {
    return param_.has_value() ? (std::clamp)(param_->interval, 1.2f, 8.0f) : 3.0f;
}

float EnemyRingBurner::GetWaveMaximumRadius() const {
    const float horizontalScale = (std::max)(baseScale_.x, baseScale_.z);
    const float configured = param_.has_value() ? param_->moveAmount : 11.0f;
    return (std::clamp)(configured, 6.0f, 18.0f) * horizontalScale;
}

void EnemyRingBurner::SpawnChargeEffects() {
    const Vector3 origin = GetWorldPosition() + Vector3{ 0.0f, 0.18f * baseScale_.y, 0.0f };
    const float scale = (std::max)(baseScale_.x, baseScale_.z);
    if (auto* meshEffects = MeshEffectManager::GetInstance()) {
        meshEffects->SpawnEffectAt(kChargeEffect, origin, { 0.0f, 0.0f, 0.0f }, { scale, baseScale_.y, scale });
    }
    if (auto* particles = GPUParticleManager::GetInstance(); particles->IsInitialized()) {
        particles->Emit(kChargeParticle, origin + Vector3{ 0.0f, 1.12f * baseScale_.y, 0.0f });
    }
}

void EnemyRingBurner::SpawnWaveEffects() {
    const Vector3 origin = GetWorldPosition() + Vector3{ 0.0f, 0.34f * baseScale_.y, 0.0f };
    const float authoredRadius = 11.0f;
    const float effectScale = GetWaveMaximumRadius() / authoredRadius;
    if (auto* meshEffects = MeshEffectManager::GetInstance()) {
        const Vector3 scale = { effectScale, baseScale_.y, effectScale };
        meshEffects->SpawnEffectAt(kWaveHeatEffect, origin, { 0.0f, 0.0f, 0.0f }, scale);
        meshEffects->SpawnEffectAt(kWaveEffect, origin, { 0.0f, 0.0f, 0.0f }, scale);
    }
    if (auto* particles = GPUParticleManager::GetInstance(); particles->IsInitialized()) {
        particles->Emit(kFireParticle, origin + Vector3{ 0.0f, 1.05f * baseScale_.y, 0.0f });
    }
}

std::unique_ptr<Object3d> EnemyRingBurner::Clone() const {
    auto clone = std::make_unique<EnemyRingBurner>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    clone->baseScale_ = baseScale_;
    clone->hasBaseScale_ = true;
    return clone;
}

void EnemyRingBurner::SetCarried(bool isCarried) {
    // 固定砲台は吸収・持ち運びの対象外です。
    (void)isCarried;
}

void EnemyRingBurner::ApplyManagedScale(const Vector3& scale) {
    baseScale_ = scale;
    hasBaseScale_ = true;
    SetScale(scale);
}

void EnemyRingBurner::CaptureReplayCustomState(json& state) const {
    BaseEnemy::CaptureReplayCustomState(state);
    state["ringBurnerState"] = static_cast<int>(state_);
    state["ringBurnerStateTimer"] = stateTimer_;
    state["ringBurnerStateDuration"] = stateDuration_;
    state["ringBurnerCooldown"] = cooldownTimer_;
    state["ringBurnerWaveRadius"] = waveRadius_;
    state["ringBurnerIdleTimer"] = idleTimer_;
    state["ringBurnerWaveDamageDone"] = waveDamageDone_;
}

void EnemyRingBurner::RestoreReplayCustomState(const json& state) {
    BaseEnemy::RestoreReplayCustomState(state);
    state_ = static_cast<State>((std::clamp)(state.value("ringBurnerState", 0), 0, 4));
    stateTimer_ = state.value("ringBurnerStateTimer", 0.0f);
    stateDuration_ = state.value("ringBurnerStateDuration", 0.0f);
    cooldownTimer_ = state.value("ringBurnerCooldown", 0.0f);
    waveRadius_ = state.value("ringBurnerWaveRadius", 0.0f);
    idleTimer_ = state.value("ringBurnerIdleTimer", 0.0f);
    waveDamageDone_ = state.value("ringBurnerWaveDamageDone", false);
}

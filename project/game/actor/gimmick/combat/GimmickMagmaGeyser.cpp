#define NOMINMAX
#include "GimmickMagmaGeyser.h"

#include "CollisionConfig.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "MeshRenderer.h"
#include "Player.h"
#include "SceneManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
constexpr float kDefaultWarningDuration = 1.35f;
constexpr float kDefaultEruptionDuration = 1.15f;
constexpr float kDefaultCooldownDuration = 2.6f;
constexpr float kDefaultDamage = 4.0f;
constexpr float kDefaultGeyserHeight = 9.5f;
constexpr float kDefaultGeyserRadius = 2.15f;
constexpr float kDefaultHorizontalKnockback = 5.5f;
constexpr float kDefaultVerticalKnockback = 15.0f;
constexpr float kDefaultSimulationWakeDistance = 110.0f;
constexpr float kVentMouthLocalHeight = 0.72f;
constexpr float kDamageRepeatInterval = 0.25f;
constexpr float kParticleInterval = 0.075f;
constexpr const char* kEruptionBurstEffect = "Resources/json/effect/effect_bakuhatu.json";

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vector4 LerpColor(const Vector4& from, const Vector4& to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
        from.z + (to.z - from.z) * t,
        from.w + (to.w - from.w) * t,
    };
}
}

void GimmickMagmaGeyser::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("MagmaGeyser");
    SetName("Gimmick_MagmaGeyser");
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    SetMaterialType(0);
    SetEmissive(1.0f);
    SetRoughness(0.72f);
    SetMetallic(0.08f);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetStatic(false);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kCylinder;
    collider.center = { 0.0f, kVentMouthLocalHeight + kDefaultGeyserHeight * 0.5f, 0.0f };
    collider.size = { kDefaultGeyserRadius, kDefaultGeyserHeight * 0.5f, kDefaultGeyserRadius };
    SetColliderConfig(collider);
    if (GetCollider()) {
        // 見た目用モデルの拡縮と危険範囲を分離し、パラメータをワールド単位で扱います。
        GetCollider()->SetScaleOverride({ 1.0f, 1.0f, 1.0f });
    }

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->speed = kDefaultDamage;
    param_->interval = kDefaultCooldownDuration;
    param_->shakeDuration = kDefaultWarningDuration;
    param_->fallDuration = kDefaultEruptionDuration;
    param_->moveAmount = kDefaultGeyserHeight;
    param_->detectionRange = kDefaultGeyserRadius;
    param_->gravity = kDefaultHorizontalKnockback;
    param_->jumpPower = kDefaultVerticalKnockback;
    param_->maxFallSpeed = kDefaultSimulationWakeDistance;
    param_->moveSpeed = 0.0f;
    param_->startActive = true;
    param_->returnOnOff = true;

    warningTelegraph_.Initialize(common);

    eruptionVisual_ = std::make_unique<Object3d>();
    eruptionVisual_->Initialize(common);
    eruptionVisual_->SetName("MagmaGeyser_FireColumn");
    eruptionVisual_->SetClassName("Effect");
    eruptionVisual_->SetModel("Primitives/plane");
    eruptionVisual_->SetColliderType(ColliderType::kNone);
    eruptionVisual_->SetCollisionAttribute(0);
    eruptionVisual_->SetCollisionMask(0);
    eruptionVisual_->SetBlendMode(BlendMode::kNormal);
    eruptionVisual_->SetMaterialType(11);
    eruptionVisual_->SetSelectedLighting(0);
    eruptionVisual_->SetEnableLighting(false);
    eruptionVisual_->SetEnableEnvMap(false);
    eruptionVisual_->SetColor({ 1.0f, 0.20f, 0.025f, 0.0f });
    eruptionVisual_->SetEmissive(2.2f);
    eruptionVisual_->SetIsVisible(false);

    if (MeshRenderer* renderer = eruptionVisual_->GetMeshRenderer()) {
        if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
            fire->effectType = 0.0f;
            fire->waveSpeed = 3.1f;
            fire->waveFrequency = 2.8f;
            fire->effectScale = 1.0f;
            fire->effectSoftness = 0.48f;
            fire->effectIntensity = 1.28f;
            fire->billboardScale = 1.0f;
            fire->effectScaleX = 0.45f;
            fire->effectScaleY = 1.0f;
            fire->effectScaleZ = 1.0f;
            fire->uvOffsetX = 2.17f;
            fire->uvOffsetY = 5.31f;
        }
    }
}

void GimmickMagmaGeyser::Update(float deltaTime) {
    if (!param_.has_value()) {
        param_.emplace();
    }

    if (!IsPlaying()) {
        ResetOutsidePlay();
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        BeginPlayCycle();
    }

    const float safeDeltaTime = (std::max)(0.0f, deltaTime);

    // 遠方の噴出口は警告、当たり判定、パーティクル更新を休止する。
    // プレイヤーが接近した時点で警告フェーズから安全に再開する。
    if (!IsPlayerWithinSimulationRange()) {
        if (!simulationSleeping_) {
            simulationSleeping_ = true;
            warningTelegraph_.Hide();
            ApplyCollisionState(false, GetGeyserHeight());
            if (eruptionVisual_) {
                eruptionVisual_->SetIsVisible(false);
            }
            SetColor(baseColor_);
            SetEmissive(baseEmissive_);
        }
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (simulationSleeping_) {
        simulationSleeping_ = false;
        if (active_) {
            BeginWarning();
        }
    }

    damageCooldownTimer_ = (std::max)(0.0f, damageCooldownTimer_ - safeDeltaTime);
    visualTimer_ += safeDeltaTime;

    if (!active_) {
        warningTelegraph_.Hide();
        ApplyCollisionState(false, GetGeyserHeight());
        if (eruptionVisual_) {
            eruptionVisual_->SetIsVisible(false);
        }
        SetColor(baseColor_);
        SetEmissive(baseEmissive_);
        warningTelegraph_.Update(safeDeltaTime);
        BaseGimmick::Update(deltaTime);
        return;
    }

    phaseTimer_ += safeDeltaTime;
    for (int transition = 0; transition < 3 && phaseTimer_ >= phaseDuration_; ++transition) {
        const float overflow = phaseTimer_ - phaseDuration_;
        AdvancePhase();
        phaseTimer_ = overflow;
    }

    switch (phase_) {
    case Phase::Warning:
        UpdateWarningVisual();
        break;
    case Phase::Erupting:
        UpdateEruptionVisual(safeDeltaTime);
        break;
    case Phase::Cooldown:
        UpdateCooldownVisual();
        break;
    }

    warningTelegraph_.Update(safeDeltaTime);
    if (eruptionVisual_ && eruptionVisual_->GetIsVisible()) {
        eruptionVisual_->Update(safeDeltaTime);
        eruptionVisual_->UpdateWorldMatrix();
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickMagmaGeyser::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    warningTelegraph_.DrawGround(pointLightResource, spotLightResource);
    BaseGimmick::Draw(pointLightResource, spotLightResource);
}

bool GimmickMagmaGeyser::OnCollision(Object3d* other) {
    if (!IsPlaying() || !active_ || phase_ != Phase::Erupting || damageCooldownTimer_ > 0.0f) {
        return true;
    }

    Player* player = dynamic_cast<Player*>(other);
    if (!player) {
        return true;
    }

    const CollisionInfo collision = CheckCollision(player);
    if (!collision.isColliding) {
        return true;
    }

    Vector3 knockbackDirection = player->GetWorldPosition() - GetMouthPosition();
    knockbackDirection.y = 0.0f;
    if (Math::Length(knockbackDirection) < 0.001f) {
        knockbackDirection = { 0.0f, 0.0f, 1.0f };
    }
    knockbackDirection = Math::Normalize(knockbackDirection);

    DamageEvent damageEvent;
    damageEvent.target = player;
    damageEvent.attacker = this;
    damageEvent.damageAmount = GetDamage();
    damageEvent.knockbackVelocity = {
        knockbackDirection.x * GetHorizontalKnockback(),
        GetVerticalKnockback(),
        knockbackDirection.z * GetHorizontalKnockback(),
    };
    damageEvent.damageType = DamageType::Fire;
    damageEvent.statusEffect.type = StatusEffectType::Burning;
    damageEvent.statusEffect.duration = 1.2f;
    damageEvent.statusEffect.tickInterval = 0.6f;
    damageEvent.statusEffect.tickDamage = 0.35f;
    EventManager::GetInstance()->Dispatch(damageEvent);

    damageCooldownTimer_ = kDamageRepeatInterval;
    return true;
}

void GimmickMagmaGeyser::OnTrigger() {
    OnSwitchEvent(!active_);
}

void GimmickMagmaGeyser::OnSwitchEvent(bool active) {
    if (!param_.has_value()) {
        param_.emplace();
    }
    if (!active && !param_->returnOnOff) {
        return;
    }

    const bool wasActive = active_;
    active_ = active;
    if (active_ && !wasActive && IsPlaying()) {
        BeginWarning();
    }
}

bool GimmickMagmaGeyser::HasOwnedSpecialMaterialVisuals() const {
    return GetIsVisible() && eruptionVisual_ && eruptionVisual_->GetIsVisible();
}

void GimmickMagmaGeyser::DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (GetIsVisible() && eruptionVisual_ && eruptionVisual_->GetIsVisible()) {
        eruptionVisual_->DrawFire(depthSrvHandle, grabSrvHandle);
    }
}

std::unique_ptr<Object3d> GimmickMagmaGeyser::Clone() const {
    auto clone = std::make_unique<GimmickMagmaGeyser>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

bool GimmickMagmaGeyser::IsPlaying() const {
    SceneManager* sceneManager = SceneManager::GetInstance();
    return sceneManager && sceneManager->IsPlaying();
}

bool GimmickMagmaGeyser::IsPlayerWithinSimulationRange() const {
    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    Player* player = scene ? scene->GetPlayer() : nullptr;
    if (!player) {
        // 敵攻撃プレビューなど、プレイヤーを持たない確認画面では従来どおり再生する。
        return true;
    }

    const Vector3 delta = player->GetWorldPosition() - GetWorldPosition();
    const float wakeDistance = GetSimulationWakeDistance();
    const float horizontalDistanceSquared = delta.x * delta.x + delta.z * delta.z;
    return horizontalDistanceSquared <= wakeDistance * wakeDistance;
}

void GimmickMagmaGeyser::BeginPlayCycle() {
    baseColor_ = GetColor();
    baseEmissive_ = GetEmissive();
    active_ = param_.has_value() ? param_->startActive : true;
    damageCooldownTimer_ = 0.0f;
    particleTimer_ = 0.0f;
    particleBandIndex_ = 0;
    visualTimer_ = 0.0f;
    simulationSleeping_ = false;
    initializedForPlay_ = true;
    SetStatic(false);

    const float initialDelay = GetInitialDelay();
    if (active_ && initialDelay > 0.0f) {
        BeginCooldown(initialDelay);
    }
    else {
        BeginWarning();
    }
}

void GimmickMagmaGeyser::ResetOutsidePlay() {
    if (initializedForPlay_) {
        SetColor(baseColor_);
        SetEmissive(baseEmissive_);
    }
    initializedForPlay_ = false;
    active_ = param_.has_value() ? param_->startActive : true;
    phase_ = Phase::Warning;
    phaseTimer_ = 0.0f;
    phaseDuration_ = GetWarningDuration();
    damageCooldownTimer_ = 0.0f;
    particleTimer_ = 0.0f;
    simulationSleeping_ = false;
    warningTelegraph_.Hide();
    ApplyCollisionState(false, GetGeyserHeight());
    if (eruptionVisual_) {
        eruptionVisual_->SetIsVisible(false);
    }
}

void GimmickMagmaGeyser::BeginWarning() {
    phase_ = Phase::Warning;
    phaseTimer_ = 0.0f;
    phaseDuration_ = GetWarningDuration();
    particleTimer_ = 0.0f;
    ApplyCollisionState(false, GetGeyserHeight());
    if (eruptionVisual_) {
        eruptionVisual_->SetIsVisible(false);
    }
}

void GimmickMagmaGeyser::BeginEruption() {
    phase_ = Phase::Erupting;
    phaseTimer_ = 0.0f;
    phaseDuration_ = GetEruptionDuration();
    damageCooldownTimer_ = 0.0f;
    particleTimer_ = 0.0f;
    particleBandIndex_ = 0;
    warningTelegraph_.Hide();
    SpawnEruptionBurst();
}

void GimmickMagmaGeyser::BeginCooldown(float duration) {
    phase_ = Phase::Cooldown;
    phaseTimer_ = 0.0f;
    phaseDuration_ = (std::max)(0.1f, duration);
    warningTelegraph_.Hide();
    ApplyCollisionState(false, GetGeyserHeight());
    if (eruptionVisual_) {
        eruptionVisual_->SetIsVisible(false);
    }
}

void GimmickMagmaGeyser::AdvancePhase() {
    switch (phase_) {
    case Phase::Warning:
        BeginEruption();
        break;
    case Phase::Erupting:
        BeginCooldown(GetCooldownDuration());
        break;
    case Phase::Cooldown:
        BeginWarning();
        break;
    }
}

void GimmickMagmaGeyser::UpdateWarningVisual() {
    const float progress = std::clamp(phaseTimer_ / phaseDuration_, 0.0f, 1.0f);
    const float pulse = 0.5f + 0.5f * std::sin(visualTimer_ * (7.0f + progress * 13.0f));
    const float warningStrength = 0.16f + progress * 0.34f + pulse * (0.10f + progress * 0.20f);
    const Vector4 warningColor = { 1.0f, 0.16f, 0.015f, baseColor_.w };

    SetColor(LerpColor(baseColor_, warningColor, warningStrength));
    SetEmissive(baseEmissive_ + 0.45f + progress * 1.45f + pulse * 0.55f);
    warningTelegraph_.ShowCircle(
        GetMouthPosition() + Vector3{ 0.0f, 0.035f, 0.0f },
        GetGeyserRadius(),
        progress,
        { 1.0f, 0.13f, 0.01f, 1.0f });
    ApplyCollisionState(false, GetGeyserHeight());
}

void GimmickMagmaGeyser::UpdateEruptionVisual(float deltaTime) {
    const float progress = std::clamp(phaseTimer_ / phaseDuration_, 0.0f, 1.0f);
    const float rise = SmoothStep01(progress / 0.16f);
    const float fall = SmoothStep01((1.0f - progress) / 0.20f);
    const float envelope = (std::min)(rise, fall);
    const float activeHeight = (std::max)(0.18f, GetGeyserHeight() * envelope);
    const float radiusPulse = 0.92f + std::sin(visualTimer_ * 17.0f) * 0.08f;

    SetColor(LerpColor(baseColor_, { 1.0f, 0.13f, 0.01f, baseColor_.w }, 0.64f + envelope * 0.24f));
    SetEmissive(baseEmissive_ + 1.25f + envelope * 1.35f);
    ApplyCollisionState(envelope > 0.08f, activeHeight);

    if (eruptionVisual_) {
        const float halfHeight = activeHeight * 0.5f;
        const float visualRadius = GetGeyserRadius() * 0.78f * radiusPulse;
        eruptionVisual_->SetTranslate(GetMouthPosition() + Vector3{ 0.0f, halfHeight, 0.0f });
        eruptionVisual_->SetScale({ halfHeight, halfHeight, halfHeight });
        eruptionVisual_->SetColor({ 1.0f, 0.18f, 0.018f, 0.68f + envelope * 0.24f });
        eruptionVisual_->SetIsVisible(envelope > 0.015f);

        if (MeshRenderer* renderer = eruptionVisual_->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                fire->effectScaleX = std::clamp((visualRadius * 2.0f) / activeHeight, 0.18f, 2.4f);
                fire->effectScaleY = 1.0f;
                fire->flowSpeedX = std::sin(visualTimer_ * 1.9f) * 0.14f;
                fire->flowSpeedY = 0.92f;
            }
        }
    }

    particleTimer_ -= deltaTime;
    if (particleTimer_ <= 0.0f) {
        EmitEruptionParticles();
        particleTimer_ += kParticleInterval;
        if (particleTimer_ <= 0.0f) {
            particleTimer_ = kParticleInterval;
        }
    }
}

void GimmickMagmaGeyser::UpdateCooldownVisual() {
    const float progress = std::clamp(phaseTimer_ / phaseDuration_, 0.0f, 1.0f);
    const float glow = (1.0f - progress) * 0.22f;
    SetColor(LerpColor(baseColor_, { 0.72f, 0.07f, 0.015f, baseColor_.w }, glow));
    SetEmissive(baseEmissive_ + glow * 1.8f);
    ApplyCollisionState(false, GetGeyserHeight());
}

void GimmickMagmaGeyser::ApplyCollisionState(bool enabled, float activeHeight) {
    Object3d::ColliderConfig collider = GetColliderConfig();
    const float height = (std::max)(0.1f, activeHeight);
    const float mouthHeight = GetMouthPosition().y - GetWorldPosition().y;
    collider.type = ColliderType::kCylinder;
    collider.center = { 0.0f, mouthHeight + height * 0.5f, 0.0f };
    collider.size = { GetGeyserRadius(), height * 0.5f, GetGeyserRadius() };
    SetColliderConfig(collider);

    SetCollisionAttribute(enabled ? CollisionAttribute::kTrigger : 0);
    SetCollisionMask(enabled ? CollisionAttribute::kPlayer : 0);
}

void GimmickMagmaGeyser::SpawnEruptionBurst() {
    const Vector3 mouth = GetMouthPosition();
    const float radius = GetGeyserRadius();

    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        const float scale = std::clamp(radius * 0.42f, 0.55f, 1.55f);
        effects->SpawnEffectAt(kEruptionBurstEffect, mouth, { 0.0f, 0.0f, 0.0f }, { scale, scale, scale });
    }

    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    particles->Emit("hit_bomb_fire_core", mouth);
    particles->Emit("hit_bomb_fire_mushroom", mouth);
    particles->Emit("fire_slime_cast", mouth);
}

void GimmickMagmaGeyser::EmitEruptionParticles() {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }

    const float height = GetGeyserHeight();
    const float bandRate = 0.10f + static_cast<float>(particleBandIndex_) * 0.28f;
    const Vector3 position = GetMouthPosition() + Vector3{ 0.0f, height * bandRate, 0.0f };
    const float speedScale = std::clamp(height / 6.5f, 0.85f, 2.2f);
    particles->EmitDirected("fire_slime_breath", position, { 0.0f, 1.0f, 0.0f }, speedScale);

    if (particleBandIndex_ == 0) {
        particles->EmitDirected("fire_slime_breath_embers", position, { 0.0f, 1.0f, 0.0f }, speedScale * 1.15f);
    }
    particleBandIndex_ = (particleBandIndex_ + 1) % 3;
}

Vector3 GimmickMagmaGeyser::GetMouthPosition() const {
    const float modelScaleY = (std::max)(0.01f, std::abs(GetScale().y));
    return GetWorldPosition() + Vector3{ 0.0f, kVentMouthLocalHeight * modelScaleY, 0.0f };
}

float GimmickMagmaGeyser::GetWarningDuration() const {
    return param_.has_value() ? (std::max)(0.2f, param_->shakeDuration) : kDefaultWarningDuration;
}

float GimmickMagmaGeyser::GetEruptionDuration() const {
    return param_.has_value() ? (std::max)(0.2f, param_->fallDuration) : kDefaultEruptionDuration;
}

float GimmickMagmaGeyser::GetCooldownDuration() const {
    return param_.has_value() ? (std::max)(0.2f, param_->interval) : kDefaultCooldownDuration;
}

float GimmickMagmaGeyser::GetInitialDelay() const {
    return param_.has_value() ? std::clamp(param_->moveSpeed, 0.0f, 30.0f) : 0.0f;
}

float GimmickMagmaGeyser::GetDamage() const {
    return param_.has_value() ? (std::max)(0.0f, param_->speed) : kDefaultDamage;
}

float GimmickMagmaGeyser::GetGeyserHeight() const {
    return param_.has_value() ? std::clamp(param_->moveAmount, 0.5f, 60.0f) : kDefaultGeyserHeight;
}

float GimmickMagmaGeyser::GetGeyserRadius() const {
    return param_.has_value() ? std::clamp(param_->detectionRange, 0.25f, 20.0f) : kDefaultGeyserRadius;
}

float GimmickMagmaGeyser::GetHorizontalKnockback() const {
    return param_.has_value() ? std::clamp(param_->gravity, 0.0f, 80.0f) : kDefaultHorizontalKnockback;
}

float GimmickMagmaGeyser::GetVerticalKnockback() const {
    return param_.has_value() ? std::clamp(param_->jumpPower, 0.0f, 100.0f) : kDefaultVerticalKnockback;
}

float GimmickMagmaGeyser::GetSimulationWakeDistance() const {
    return param_.has_value()
        ? std::clamp(param_->maxFallSpeed, 20.0f, 500.0f)
        : kDefaultSimulationWakeDistance;
}

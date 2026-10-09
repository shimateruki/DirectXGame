#define NOMINMAX
#include "GimmickMagmaGeyser.h"

#include "CollisionConfig.h"
#include "CameraManager.h"
#include "BulletManager.h"
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
} // namespace

void GimmickMagmaGeyser::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("MagmaGeyser");
    SetName("Gimmick_MagmaGeyser");
    SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    SetMaterialType(0);
    SetEmissive(1.0f);
    SetRoughness(0.72f);
    SetMetallic(0.08f);
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetStatic(false);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kCylinder;
    collider.center = {0.0f, kVentMouthLocalHeight + kDefaultGeyserHeight * 0.5f, 0.0f};
    collider.size = {kDefaultGeyserRadius, kDefaultGeyserHeight * 0.5f, kDefaultGeyserRadius};
    SetColliderConfig(collider);
    if (GetCollider()) {
        // 見た目用モデルの拡縮と危険範囲を分離し、パラメータをワールド単位で扱います。
        GetCollider()->SetScaleOverride({1.0f, 1.0f, 1.0f});
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

    for (auto& bubble : boilingVisuals_) {
        bubble = std::make_unique<Object3d>();
        bubble->Initialize(common);
        bubble->SetName("MagmaGeyser_Bubble");
        bubble->SetModel("Primitives/sphere");
        bubble->SetColliderType(ColliderType::kNone);
        bubble->SetCollisionAttribute(0);
        bubble->SetCollisionMask(0);
        bubble->SetCastShadow(false);
        bubble->SetMaterialType(9);
        bubble->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
        bubble->SetIsVisible(false);
        auto* material = bubble->GetMeshRenderer()->GetWaterParamData();
        material->effectType = 4.0f;
        material->waveHeight = 0.0f;
        material->effectIntensity = 1.0f;
    }

    eruptionVisual_ = std::make_unique<Object3d>();
    eruptionVisual_->Initialize(common);
    eruptionVisual_->SetName("MagmaGeyser_FireColumn");
    eruptionVisual_->SetClassName("Effect");
    eruptionVisual_->SetModel("Stages/magma_pressure_column");
    eruptionVisual_->SetColliderType(ColliderType::kNone);
    eruptionVisual_->SetCollisionAttribute(0);
    eruptionVisual_->SetCollisionMask(0);
    eruptionVisual_->SetBlendMode(BlendMode::kNormal);
    eruptionVisual_->SetMaterialType(9);
    eruptionVisual_->SetSelectedLighting(0);
    eruptionVisual_->SetEnableLighting(false);
    eruptionVisual_->SetEnableEnvMap(false);
    eruptionVisual_->SetColor({1.0f, 0.20f, 0.025f, 0.0f});
    eruptionVisual_->SetEmissive(2.2f);
    eruptionVisual_->SetIsVisible(false);

    if (MeshRenderer* renderer = eruptionVisual_->GetMeshRenderer()) {
        if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
            fire->effectType = 3.0f;
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
        UpdateBoilingVisual(deltaTime, 0.18f, true);
        return;
    }

    if (!initializedForPlay_) {
        BeginPlayCycle();
    }

    const float safeDeltaTime = (std::max)(0.0f, deltaTime);
    if (singleEruption_) {
        UpdateSingleEruption(safeDeltaTime);
        BaseGimmick::Update(deltaTime);
        UpdateBoilingVisual(safeDeltaTime, std::clamp(singleTimer_ / GetWarningDuration(), 0.0f, 1.0f),
                            singleEruption_ && !singleFired_);
        return;
    }

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
        UpdateBoilingVisual(0.0f, 0.0f, false);
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
        // 信号待ちの発射口も弱く泡立て、近づいた時に場所が読めるようにする。
        UpdateBoilingVisual(safeDeltaTime, 0.04f, true);
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
    const float pressure = phase_ == Phase::Warning ? GetWarningProgress() : 0.06f;
    UpdateBoilingVisual(safeDeltaTime, pressure, phase_ != Phase::Erupting);
}

void GimmickMagmaGeyser::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    warningTelegraph_.DrawGround(pointLightResource, spotLightResource);
    if (!ventInLiquid_ || GetModelName() != "Stages/magma_vent") {
        BaseGimmick::Draw(pointLightResource, spotLightResource);
    }
}

void GimmickMagmaGeyser::DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource,
                                       ID3D12Resource* spotLightResource, int previewBufferIndex) {
    if (!ventInLiquid_ || GetModelName() != "Stages/magma_vent") {
        BaseGimmick::DrawForCamera(camera, pointLightResource, spotLightResource, previewBufferIndex);
    }
}

bool GimmickMagmaGeyser::OnCollision(Object3d* other) {
    if (!IsPlaying() || !active_ || (param_ && param_->actionMode != 0) || phase_ != Phase::Erupting ||
        damageCooldownTimer_ > 0.0f) {
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
        knockbackDirection = {0.0f, 0.0f, 1.0f};
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

void GimmickMagmaGeyser::OnTrigger() { OnSwitchEvent(!active_); }

void GimmickMagmaGeyser::OnSwitchEvent(bool active) {
    if (!param_.has_value()) {
        param_.emplace();
    }
    if (!active && !param_->returnOnOff) {
        return;
    }

    const bool wasActive = active_;
    active_ = active;
    if (!active_)
        HideBoilingVisuals();
    if (active_ && !wasActive && IsPlaying()) {
        BeginWarning();
    }
}

bool GimmickMagmaGeyser::HasOwnedSpecialMaterialVisuals() const {
    if (!GetIsVisible())
        return false;
    if (eruptionVisual_ && eruptionVisual_->GetIsVisible())
        return true;
    for (const auto& bubble : boilingVisuals_) {
        if (bubble && bubble->GetIsVisible())
            return true;
    }
    return false;
}

void GimmickMagmaGeyser::DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) {
    if (!GetIsVisible())
        return;
    for (const auto& bubble : boilingVisuals_) {
        if (bubble && bubble->GetIsVisible())
            bubble->DrawMagma(depthSrvHandle, grabSrvHandle);
    }
    if (GetIsVisible() && eruptionVisual_ && eruptionVisual_->GetIsVisible()) {
        if (param_ && param_->actionMode == 2) {
            if (auto* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                for (const auto& object : scene->GetObjects()) {
                    if (object && object->GetGimmickType() == "MovingFloor" && GetEventID() >= 0 &&
                        object->GetTargetID() == GetEventID()) {
                        Vector3 scale = eruptionVisual_->GetScale();
                        scale.y = (std::max)(0.1f, object->GetAABB().min.y - GetMouthPosition().y + 0.15f);
                        eruptionVisual_->SetScale(scale);
                        eruptionVisual_->UpdateLocalMatrix();
                        eruptionVisual_->UpdateWorldMatrix();
                        break;
                    }
                }
            }
        }
        eruptionVisual_->DrawMagma(depthSrvHandle, grabSrvHandle);
    }
}

void GimmickMagmaGeyser::DrawOwnedSpecialMaterialVisualsForCamera(Camera* camera, uint32_t depthSrvHandle,
                                                                  uint32_t grabSrvHandle, int previewBufferIndex) {
    if (!GetIsVisible())
        return;
    for (const auto& bubble : boilingVisuals_) {
        if (bubble && bubble->GetIsVisible())
            bubble->DrawSpecialMaterialForCamera(camera, depthSrvHandle, grabSrvHandle, previewBufferIndex);
    }
    if (eruptionVisual_ && eruptionVisual_->GetIsVisible()) {
        eruptionVisual_->DrawSpecialMaterialForCamera(camera, depthSrvHandle, grabSrvHandle, previewBufferIndex);
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
    } else {
        BeginWarning();
    }
}

void GimmickMagmaGeyser::ResetOutsidePlay() {
    singleEruption_ = singleFired_ = false;
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
    HideBoilingVisuals();
    phase_ = Phase::Erupting;
    phaseTimer_ = 0.0f;
    phaseDuration_ = GetEruptionDuration();
    damageCooldownTimer_ = 0.0f;
    particleTimer_ = 0.0f;
    particleBandIndex_ = 0;
    warningTelegraph_.Hide();
    if (param_ && param_->actionMode == 1)
        FireMagmaProjectiles();
    else
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
    const Vector4 warningColor = {1.0f, 0.16f, 0.015f, baseColor_.w};

    SetColor(LerpColor(baseColor_, warningColor, warningStrength));
    SetEmissive(baseEmissive_ + 0.45f + progress * 1.45f + pulse * 0.55f);
    // 円のUIではなく、同じ警告進行度で液面の泡を大きくする。
    warningTelegraph_.Hide();
    ApplyCollisionState(false, GetGeyserHeight());
}

void GimmickMagmaGeyser::UpdateEruptionVisual(float deltaTime) {
    // 火の玉は独立して往復するため、同じ場所に噴射柱を重ねない。
    if (param_ && param_->actionMode == 1) {
        if (eruptionVisual_)
            eruptionVisual_->SetIsVisible(false);
        ApplyCollisionState(false, GetGeyserHeight());
        return;
    }
    const float envelope = GetPressureEnvelope();
    const float activeHeight = (std::max)(0.18f, GetGeyserHeight() * envelope);
    const float radiusPulse = 0.92f + std::sin(visualTimer_ * 17.0f) * 0.08f;

    SetColor(LerpColor(baseColor_, {1.0f, 0.13f, 0.01f, baseColor_.w}, 0.64f + envelope * 0.24f));
    SetEmissive(baseEmissive_ + 1.25f + envelope * 1.35f);
    // 岩盤駆動と火山弾モードは、噴射柱そのものに攻撃判定を持たせない。
    ApplyCollisionState(param_->actionMode == 0 && envelope > 0.08f, activeHeight);

    if (eruptionVisual_) {
        const float visualRadius = GetGeyserRadius() * 0.78f * radiusPulse;
        // 専用メッシュは底面Y=0、上面Y=1。岩盤用は下端まで柱を伸ばす。
        float columnHeight = activeHeight;
        if (param_->actionMode == 2) {
            if (auto* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                for (const auto& object : scene->GetObjects()) {
                    if (object && object->GetGimmickType() == "MovingFloor" && object->GetTargetID() == GetEventID() &&
                        GetEventID() >= 0) {
                        columnHeight = (std::max)(0.1f, object->GetWorldPosition().y - GetMouthPosition().y - 0.8f);
                        break;
                    }
                }
            }
        }
        eruptionVisual_->SetTranslate(GetMouthPosition());
        eruptionVisual_->SetScale({visualRadius, columnHeight, visualRadius});
        eruptionVisual_->SetColor({1.0f, 0.18f, 0.018f, 0.68f + envelope * 0.24f});
        eruptionVisual_->SetIsVisible(envelope > 0.015f);

        if (MeshRenderer* renderer = eruptionVisual_->GetMeshRenderer()) {
            if (MeshRenderer::WaterParamForGPU* fire = renderer->GetWaterParamData()) {
                fire->effectScaleX = std::clamp((visualRadius * 2.0f) / activeHeight, 0.18f, 2.4f);
                fire->effectScaleY = 1.0f;
                fire->flowSpeedX = std::sin(visualTimer_ * 1.9f) * 0.14f;
                fire->flowSpeedY = 12.0f;
                fire->waveHeight = 0.0f;
                fire->waveFrequency = 2.8f;
                fire->effectIntensity = 1.45f;
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
    SetColor(LerpColor(baseColor_, {0.72f, 0.07f, 0.015f, baseColor_.w}, glow));
    SetEmissive(baseEmissive_ + glow * 1.8f);
    ApplyCollisionState(false, GetGeyserHeight());
}

void GimmickMagmaGeyser::HideBoilingVisuals() {
    for (auto& bubble : boilingVisuals_) {
        if (bubble)
            bubble->SetIsVisible(false);
    }
}

void GimmickMagmaGeyser::UpdateBoilingVisual(float deltaTime, float pressure, bool visible) {
    // マグマ面の実際の高さに合わせ、沈んだ噴出口の四角い縁は描かない。
    Vector3 surface = GetMouthPosition();
    ventInLiquid_ = false;
    const float radius = GetGeyserRadius();
    auto* manager = SceneManager::GetInstance();
    auto* scene = manager ? manager->GetCurrentScene() : nullptr;
    if (scene) {
        for (const auto& object : scene->GetObjects()) {
            if (!object || object.get() == this || object->GetMaterialType() != 9 || !object->GetIsVisible())
                continue;
            const AABB bounds = object->GetModelWorldAABB();
            if (surface.x < bounds.min.x || surface.x > bounds.max.x || surface.z < bounds.min.z ||
                surface.z > bounds.max.z || std::abs(bounds.max.y - surface.y) > radius)
                continue;
            surface.y = bounds.max.y;
            ventInLiquid_ = true;
            break;
        }
    }
    surface.y += 0.025f;
    if (!visible || !GetIsVisible()) {
        HideBoilingVisuals();
        return;
    }

    float width = 1.0f, height = 1.0f, speed = 0.68f;
    if (auto* renderer = GetMeshRenderer()) {
        if (auto* params = renderer->GetWaterParamData()) {
            width = std::clamp(params->effectScaleX, 0.25f, 2.0f);
            height = std::clamp(params->effectScaleY, 0.25f, 2.0f);
            speed = std::clamp(params->waveSpeed, 0.1f, 3.0f);
        }
    }
    pressure = std::clamp(pressure, 0.0f, 1.0f);
    boilingClock_ = std::fmod(boilingClock_ + (std::max)(0.0f, deltaTime) * speed * (0.9f + pressure * 1.8f), 1000.0f);
    for (size_t i = 0; i < boilingVisuals_.size(); ++i) {
        auto& bubble = boilingVisuals_[i];
        if (!bubble)
            continue;
        const float index = static_cast<float>(i);
        const float cycle = std::fmod(boilingClock_ + index * 0.27f, 1.0f);
        const float growth = SmoothStep01(cycle / 0.72f);
        float pop = SmoothStep01((cycle - 0.72f) / 0.28f);
        float bulge = growth * (1.0f - pop);
        const float angle = index * 2.399963f + GetRotation().y;
        Vector3 position = surface;
        if (i > 0) {
            position.x += std::cos(angle) * radius * 0.85f * width;
            position.z += std::sin(angle) * radius * 0.85f * width;
        }
        float bubbleRadius = radius * width * (i == 0 ? 0.40f : 0.18f) * (0.70f + pressure * 0.30f) *
                             (0.6f + growth * 0.4f + pop * 0.32f);
        float bubbleHeight = radius * height * (i == 0 ? 0.30f : 0.19f) * (0.45f + pressure * 0.55f) * bulge;
        // 噴出直前だけ中央に大きな一泡を溜め、発射・上昇と同時に割る。
        if (i == 0 && pressure > 0.65f) {
            const float swell = SmoothStep01((pressure - 0.65f) / 0.35f);
            bubbleRadius = radius * width * (0.38f + swell * 0.18f);
            bubbleHeight = radius * height * (0.14f + swell * 0.36f);
            pop = 0.0f;
            bulge = 1.0f;
        }
        const float safeHeight = (std::max)(0.015f, bubbleHeight);
        // 球の下半分を液面で切り、浮遊する球ではなく膨らんだ液面にする。
        position.y -= safeHeight * 0.12f;
        const bool slimeHead = i == 0 && param_ && param_->actionMode == 1;
        const char* desiredModel = slimeHead ? "Characters/magma_hopper" : "Primitives/sphere";
        if (bubble->GetModelName() != desiredModel)
            bubble->SetModel(desiredModel);
        if (slimeHead) {
            // 同じモデルが液面から顔を出し、そのまま噴出へつながる。
            const float base = std::clamp(param_->healAmount, 0.25f, 2.0f) * 1.3f;
            position = surface;
            position.y += base * (-0.08f + pressure * 0.48f);
            bubble->SetScale({base, base * (0.62f + pressure * 0.24f), base});
            if (auto* camera = CameraManager::GetInstance()->GetActiveCamera()) {
                const Vector3 towardCamera = camera->GetEye() - position;
                if (towardCamera.x * towardCamera.x + towardCamera.z * towardCamera.z > 0.001f)
                    bubble->SetRotation({0.0f, std::atan2(-towardCamera.x, -towardCamera.z), 0.0f});
            }
        } else {
            bubble->SetScale({bubbleRadius, safeHeight, bubbleRadius});
            bubble->SetRotation({0.0f, 0.0f, 0.0f});
        }
        bubble->SetTranslate(position);
        bubble->SetIsVisible(slimeHead || bulge > 0.025f);
        auto* material = bubble->GetMeshRenderer()->GetWaterParamData();
        material->effectType = slimeHead ? 5.0f : 4.0f;
        material->effectScaleY = surface.y;
        material->effectSoftness = pop;
        material->effectIntensity = 0.90f + pressure * 0.16f;
        bubble->Update((std::max)(0.0f, deltaTime));
        bubble->UpdateLocalMatrix();
        bubble->UpdateWorldMatrix();
    }
}

void GimmickMagmaGeyser::ApplyCollisionState(bool enabled, float activeHeight) {
    Object3d::ColliderConfig collider = GetColliderConfig();
    const float height = (std::max)(0.1f, activeHeight);
    const float mouthHeight = GetMouthPosition().y - GetWorldPosition().y;
    collider.type = ColliderType::kCylinder;
    collider.center = {0.0f, mouthHeight + height * 0.5f, 0.0f};
    collider.size = {GetGeyserRadius(), height * 0.5f, GetGeyserRadius()};
    SetColliderConfig(collider);

    SetCollisionAttribute(enabled ? CollisionCategories::Get("trigger") : 0);
    SetCollisionMask(enabled ? CollisionCategories::Get("player") : 0);
}

void GimmickMagmaGeyser::SpawnEruptionBurst() {
    const Vector3 mouth = GetMouthPosition();
    const float radius = GetGeyserRadius();

    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        const float scale = std::clamp(radius * 0.42f, 0.55f, 1.55f);
        effects->SpawnEffectAt(kEruptionBurstEffect, mouth, {0.0f, 0.0f, 0.0f}, {scale, scale, scale});
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

    // 岩盤用は実際の液柱全体へ飛沫を配置し、根元だけの炎に見せない。
    const float height = (param_ && param_->actionMode == 2 && eruptionVisual_)
                             ? eruptionVisual_->GetScale().y
                             : GetGeyserHeight() * GetPressureEnvelope();
    const float bandRate = 0.10f + static_cast<float>(particleBandIndex_) * 0.28f;
    const Vector3 position = GetMouthPosition() + Vector3{0.0f, height * bandRate, 0.0f};
    const float speedScale = std::clamp(height / 6.5f, 0.85f, 2.2f);
    particles->EmitDirected("fire_slime_breath", position, {0.0f, 1.0f, 0.0f}, speedScale);

    if (particleBandIndex_ == 0) {
        particles->EmitDirected("fire_slime_breath_embers", position, {0.0f, 1.0f, 0.0f}, speedScale * 1.15f);
    }
    particleBandIndex_ = (particleBandIndex_ + 1) % 3;
}

Vector3 GimmickMagmaGeyser::GetMouthPosition() const {
    const float modelScaleY = (std::max)(0.01f, std::abs(GetScale().y));
    Vector3 mouth = GetWorldPosition() + Vector3{0.0f, kVentMouthLocalHeight * modelScaleY, 0.0f};
    // 上昇液面へリンクしたスライムは、泡と発射位置の両方を現在の液面へ合わせる。
    if (param_ && param_->actionMode == 1 && GetTargetID() >= 0) {
        auto* manager = SceneManager::GetInstance();
        auto* scene = manager ? manager->GetCurrentScene() : nullptr;
        auto* liquid = scene ? scene->FindObjectByEventID(GetTargetID()) : nullptr;
        if (liquid && liquid->GetGimmickType() == "LiquidLevel") {
            const AABB bounds = liquid->GetModelWorldAABB();
            if (mouth.x >= bounds.min.x && mouth.x <= bounds.max.x && mouth.z >= bounds.min.z &&
                mouth.z <= bounds.max.z)
                mouth.y = bounds.max.y;
        }
    }
    return mouth;
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
    return param_.has_value() ? std::clamp(param_->maxFallSpeed, 20.0f, 500.0f) : kDefaultSimulationWakeDistance;
}

float GimmickMagmaGeyser::GetPressureEnvelope() const {
    if (!initializedForPlay_ || !active_ || simulationSleeping_ || phase_ != Phase::Erupting)
        return 0.0f;
    const float progress = std::clamp(phaseTimer_ / (std::max)(0.2f, phaseDuration_), 0.0f, 1.0f);
    return (std::min)(SmoothStep01(progress / 0.25f), SmoothStep01((1.0f - progress) / 0.25f));
}

float GimmickMagmaGeyser::GetLiftOffset() const { return GetGeyserHeight() * GetPressureEnvelope(); }

float GimmickMagmaGeyser::GetFireballFlightDuration() const {
    const float radius = param_ ? std::clamp(param_->healAmount, 0.25f, 2.0f) : 0.65f;
    const float speed = param_ ? std::clamp(param_->jumpPower, 8.0f, 45.0f) : 20.0f;
    // 発射中心から見た液面下の消滅位置。球全体が戻ってから消す。
    return (speed + std::sqrt(speed * speed + 40.0f * radius * 1.8f)) / 20.0f;
}

void GimmickMagmaGeyser::FireMagmaProjectiles() {
    if (!param_)
        return;
    const float radius = std::clamp(param_->healAmount, 0.25f, 2.0f);
    const float verticalSpeed = std::clamp(param_->jumpPower, 8.0f, 45.0f);
    BulletVisualConfig visual;
    visual.materialType = 9;
    visual.effectType = 5.0f;
    visual.waveHeight = 0.0f;
    visual.color = {1.0f, 1.0f, 1.0f, 1.0f};
    visual.visualScale = radius * 1.3f;
    visual.emissive = 1.8f;
    visual.effectIntensity = 1.0f;
    visual.billboardScale = 1.0f;
    visual.trailPreset = "";
    visual.impactPreset = "magma_slime_rush_splash";
    visual.trailInterval = 0.10f;
    visual.magmaHopperPresentation = true;
    // 一つの予兆から一発だけ垂直に噴き出し、重力で同じ場所へ戻す。
    const Vector3 mouth = GetMouthPosition();
    const Vector3 spawnPosition = mouth + Vector3{0.0f, radius * 0.5f, 0.0f};
    BulletManager::GetInstance()->Fire(spawnPosition, {0.0f, verticalSpeed, 0.0f}, CollisionCategories::Get("enemy_attack"),
                                       CollisionCategories::Get("player") | CollisionCategories::Get("ground"), "Characters/magma_hopper", radius,
                                       GetFireballFlightDuration(), visual, GetDamage(), {}, DamageType::Fire, 20.0f);
    if (auto* particles = GPUParticleManager::GetInstance(); particles && particles->IsInitialized()) {
        particles->Emit("magma_slime_rush_splash", mouth);
    }
}

float GimmickMagmaGeyser::GetWarningProgress() const {
    if (!initializedForPlay_ || !active_ || simulationSleeping_ || phase_ != Phase::Warning)
        return 0.0f;
    return std::clamp(phaseTimer_ / (std::max)(0.2f, phaseDuration_), 0.0f, 1.0f);
}

void GimmickMagmaGeyser::ResetEscapeCycle(bool active) {
    HideBoilingVisuals();
    boilingClock_ = 0.0f;
    singleEruption_ = singleFired_ = false;
    if (!initializedForPlay_)
        BeginPlayCycle();
    // 待機からの復帰で個別の初回遅延を上書きしないようにします。
    simulationSleeping_ = false;
    active_ = active;
    damageCooldownTimer_ = particleTimer_ = visualTimer_ = 0.0f;
    particleBandIndex_ = 0;
    warningTelegraph_.Hide();
    if (eruptionVisual_)
        eruptionVisual_->SetIsVisible(false);
    ApplyCollisionState(false, GetGeyserHeight());
    BeginCooldown(active ? (std::max)(0.05f, GetInitialDelay()) : GetCooldownDuration());
}

void GimmickMagmaGeyser::RequestFireballEruption() {
    if (!IsPlaying() || singleEruption_)
        return;
    if (!initializedForPlay_)
        BeginPlayCycle();
    singleTimer_ = 0.0f;
    singleEruption_ = true;
    singleFired_ = false;
    active_ = false;
    warningTelegraph_.Hide();
    ApplyCollisionState(false, GetGeyserHeight());
    if (eruptionVisual_)
        eruptionVisual_->SetIsVisible(false);
}

void GimmickMagmaGeyser::UpdateSingleEruption(float deltaTime) {
    singleTimer_ += deltaTime;
    const float warning = GetWarningDuration();
    if (!singleFired_ && singleTimer_ >= warning) {
        FireMagmaProjectiles();
        singleFired_ = true;
    }
    // 横方向の着弾予告は出さず、発射口の泡だけで予告する。
    warningTelegraph_.Hide();
    const float progress = std::clamp(singleTimer_ / warning, 0.0f, 1.0f);
    SetEmissive(baseEmissive_ + (singleFired_ ? 0.0f : progress * 1.5f));
    if (singleTimer_ >= warning + GetFireballFlightDuration()) {
        singleEruption_ = false;
        SetEmissive(baseEmissive_);
    }
}

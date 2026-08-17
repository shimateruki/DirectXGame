#include "GimmickStageGate.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include "StageManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
constexpr int kGateModeStageSelectNode = 0;
constexpr int kGateModeSceneTransition = 1;
constexpr int kGateModeStageTransition = 2;
constexpr int kGatePortalMaterialType = 22;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float Smoothstep(float edge0, float edge1, float value) {
    const float rawWidth = edge1 - edge0;
    const float width = rawWidth > 0.0001f ? rawWidth : 0.0001f;
    const float t = Clamp01((value - edge0) / width);
    return t * t * (3.0f - 2.0f * t);
}
}

void GimmickStageGate::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("StageGate");
    SetName("Gimmick_StageGate");
    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->gimmickType = "StageGate";
    param_->targetScene = param_->targetScene.empty() ? "SELECT" : param_->targetScene;
    param_->startActive = true;
    SetColor({ 0.35f, 0.75f, 1.0f, 1.0f });
    SetScale({ 1.4f, 1.4f, 1.4f });
    SetEmissive(1.8f);
    SetMaterialType(kGatePortalMaterialType);
    SetBlendMode(BlendMode::kNormal);
    SetEnableEnvMap(false);

    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(4.0f);
}

void GimmickStageGate::Update(float deltaTime) {
    CaptureBaseScale();
    baseScale_ = GetScale();

    pulseTimer_ += deltaTime;
    const float response = Clamp01(deltaTime * 6.5f);
    const float previousActivation = activation_;
    activation_ = Lerp(activation_, targetActivation_, response);
    if (isUnlocked_ && targetActivation_ > 0.82f && previousActivation < 0.62f && activation_ >= 0.62f) {
        activationBurst_ = 1.0f;
    }
    activationBurst_ = activationBurst_ > deltaTime * 1.15f ? activationBurst_ - deltaTime * 1.15f : 0.0f;
    const float visibleActivation = isUnlocked_ ? activation_ : 0.06f;

    lastAppliedPulse_ = 1.0f;
    Vector4 gateColor = GetTargetColor();
    if (activationBurst_ > 0.0f && isUnlocked_) {
        const float burstTint = Clamp01(activationBurst_ * 0.82f);
        const Vector4 burstColor = isCleared_ ? Vector4{ 0.58f, 1.0f, 1.0f, 1.0f } : Vector4{ 1.0f, 0.88f, 0.40f, 1.0f };
        gateColor.x = Lerp(gateColor.x, burstColor.x, burstTint);
        gateColor.y = Lerp(gateColor.y, burstColor.y, burstTint);
        gateColor.z = Lerp(gateColor.z, burstColor.z, burstTint);
        gateColor.w = Lerp(gateColor.w, 1.0f, burstTint);
    }
    SetColor(gateColor);
    float emissive = isUnlocked_ ? Lerp(0.62f, 1.65f, visibleActivation) : 0.34f;
    if (isSelected_ && isUnlocked_) emissive = Lerp(emissive, 2.7f, visibleActivation);
    if (isCleared_) emissive = Lerp(emissive, 1.9f, visibleActivation);
    if (isUnlocking_) emissive = 4.0f + std::sin(pulseTimer_ * 10.0f) * 0.9f;
    if (isUnlocked_) {
        emissive += activationBurst_ * 2.8f;
    }
    SetEmissive(emissive);
    UpdatePortalMaterial();

    BaseGimmick::Update(deltaTime);
}

bool GimmickStageGate::OnCollision(Object3d* other) {
    if (!other || !(other->GetCollisionAttribute() & CollisionAttribute::kPlayer)) {
        return false;
    }
    if (!CanTriggerTransition()) {
        return false;
    }

    TriggerTransition();
    return true;
}

std::unique_ptr<Object3d> GimmickStageGate::Clone() const {
    auto newObj = std::make_unique<GimmickStageGate>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

int GimmickStageGate::GetStageIndex() const {
    return GetTargetID();
}

int GimmickStageGate::GetGateMode() const {
    if (!param_.has_value()) {
        return kGateModeStageSelectNode;
    }
    return param_->actionMode;
}

const std::string& GimmickStageGate::GetTargetSceneName() const {
    static const std::string kDefaultScene = "SELECT";
    if (!param_.has_value() || param_->targetScene.empty()) {
        return kDefaultScene;
    }
    return param_->targetScene;
}

bool GimmickStageGate::IsStageSelectNodeMode() const {
    return GetGateMode() == kGateModeStageSelectNode;
}

void GimmickStageGate::SetGateState(bool selected, bool unlocked, bool cleared, bool unlocking) {
    isSelected_ = selected;
    isUnlocked_ = unlocked;
    isCleared_ = cleared;
    isUnlocking_ = unlocking;
}

void GimmickStageGate::SetGateActivation(float activation) {
    targetActivation_ = Clamp01(activation);
}

void GimmickStageGate::SetTransitionEnabled(bool enabled) {
    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->startActive = enabled;
    if (enabled) {
        hasTriggeredTransition_ = false;
    }
}

void GimmickStageGate::TriggerEntryReaction() {
    // プレイヤーが触れた瞬間に、ゲートを強く反応させます。
    targetActivation_ = 1.0f;
    activation_ = (std::max)(activation_, 0.92f);
    activationBurst_ = 1.0f;
}
void GimmickStageGate::UpdatePortalMaterial() {
    auto* renderer = GetMeshRenderer();
    if (!renderer || !renderer->GetWaterParamData()) {
        return;
    }

    auto* portal = renderer->GetWaterParamData();
    const float active = isUnlocking_ ? 1.0f : (isUnlocked_ ? activation_ : 0.0f);
    const float selectedBoost = (isSelected_ && isUnlocked_) ? 1.0f : 0.0f;
    const float entryBurst = isUnlocked_ ? activationBurst_ : 0.0f;

    portal->waveSpeed = Lerp(0.28f, 2.25f, active) + entryBurst * 2.30f;
    portal->waveHeight = Lerp(0.18f, 1.35f, active) + entryBurst * 0.72f;
    portal->waveFrequency = Lerp(4.0f, 22.0f, active) + entryBurst * 9.0f;
    portal->effectSoftness = Lerp(0.28f, 0.66f, active) + entryBurst * 0.10f;
    portal->effectIntensity = isUnlocked_
        ? Lerp(0.34f, 1.58f + selectedBoost * 0.44f, active) + entryBurst * 1.85f
        : 0.12f;
    portal->effectScale = Lerp(0.78f, 1.18f, active) + entryBurst * 0.18f;
    portal->effectType = isCleared_ ? 2.0f : 1.0f;
    portal->flowSpeedX = 0.0f;
    portal->flowSpeedY = 0.0f;
    portal->uvOffsetX = activationBurst_;
    portal->uvOffsetY = active;
}

bool GimmickStageGate::CanTriggerTransition() const {
    const int mode = GetGateMode();
    if (mode == kGateModeStageSelectNode || hasTriggeredTransition_) {
        return false;
    }
    if (param_.has_value() && !param_->startActive) {
        return false;
    }
    return true;
}

void GimmickStageGate::TriggerTransition() {
    hasTriggeredTransition_ = true;

    if (GetGateMode() == kGateModeStageTransition) {
        StageManager::GetInstance()->SetCurrentStage(GetStageIndex());
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        return;
    }

    SceneManager::GetInstance()->ChangeScene(GetTargetSceneName());
}

void GimmickStageGate::CaptureBaseScale() {
    if (hasBaseScale_) {
        return;
    }
    baseScale_ = GetScale();
    hasBaseScale_ = true;
}

Vector4 GimmickStageGate::GetTargetColor() const {
    if (!isUnlocked_) {
        // 未解放でもゲートの輪郭は残し、鎖と南京錠で封鎖状態を示します。
        return { 0.30f, 0.54f, 0.64f, 0.92f };
    }
    const float active = isUnlocking_ ? 1.0f : activation_;
    if (isUnlocking_) {
        return { 1.0f, 0.72f, 0.28f, 1.0f };
    }
    if (isCleared_) {
        const float alpha = Lerp(0.70f, 0.98f, active);
        if (isSelected_) {
            return { 0.48f, 0.96f, 1.0f, alpha };
        }
        return { 0.28f, 0.82f, 1.0f, alpha };
    }
    if (isSelected_) {
        return { 1.0f, 0.62f, 0.24f, Lerp(0.78f, 1.0f, active) };
    }
    return { 1.0f, 0.52f, 0.20f, Lerp(0.68f, 0.94f, active) };
}

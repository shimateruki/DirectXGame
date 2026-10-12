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
    // 補間係数が範囲を超えてゲートの演出値を壊さないようにします。
    return std::clamp(value, 0.0f, 1.0f);
}

float Lerp(float a, float b, float t) {
    // 開放演出の各値を同じ割合で遷移させます。
    return a + (b - a) * t;
}

float Smoothstep(float edge0, float edge1, float value) {
    // 閾値の重複時もゼロ除算せず、境界で滑らかに変化させます。
    const float rawWidth = edge1 - edge0;
    const float width = rawWidth > 0.0001f ? rawWidth : 0.0001f;
    const float t = Clamp01((value - edge0) / width);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

void GimmickStageGate::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 未設定の遷移先はステージ選択にし、ポータル専用材質を割り当てます。
    SetClassName("Gimmick");
    SetGimmickType("StageGate");
    SetName("Gimmick_StageGate");
    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->gimmickType = "StageGate";
    param_->targetScene = param_->targetScene.empty() ? "SELECT" : param_->targetScene;
    param_->startActive = true;
    SetColor({0.35f, 0.75f, 1.0f, 1.0f});
    SetScale({1.4f, 1.4f, 1.4f});
    SetEmissive(1.8f);
    SetMaterialType(kGatePortalMaterialType);
    SetBlendMode(BlendMode::kNormal);
    SetEnableEnvMap(false);

    // プレイヤーだけがゲートへの入場を開始できます。
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(4.0f);
}

void GimmickStageGate::Update(float deltaTime) {
    // 目標開放度に滑らかに追従させ、開放の閾値をまたいだ瞬間だけ発光を強めます。
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
        const Vector4 burstColor = isCleared_ ? Vector4{0.58f, 1.0f, 1.0f, 1.0f} : Vector4{1.0f, 0.88f, 0.40f, 1.0f};
        gateColor.x = Lerp(gateColor.x, burstColor.x, burstTint);
        gateColor.y = Lerp(gateColor.y, burstColor.y, burstTint);
        gateColor.z = Lerp(gateColor.z, burstColor.z, burstTint);
        gateColor.w = Lerp(gateColor.w, 1.0f, burstTint);
    }
    SetColor(gateColor);
    float emissive = isUnlocked_ ? Lerp(0.62f, 1.65f, visibleActivation) : 0.34f;
    if (isSelected_ && isUnlocked_)
        emissive = Lerp(emissive, 2.7f, visibleActivation);
    if (isCleared_)
        emissive = Lerp(emissive, 1.9f, visibleActivation);
    if (isUnlocking_)
        emissive = 4.0f + std::sin(pulseTimer_ * 10.0f) * 0.9f;
    if (isUnlocked_) {
        emissive += activationBurst_ * 2.8f;
    }
    SetEmissive(emissive);
    UpdatePortalMaterial();

    BaseGimmick::Update(deltaTime);
}

bool GimmickStageGate::OnCollision(Object3d* other) {
    // ステージ選択用の展示ノードは、触れてもシーン遷移を起こしません。
    if (!other || !(other->GetCollisionAttribute() & CollisionCategories::Get("player"))) {
        return false;
    }
    if (!CanTriggerTransition()) {
        return false;
    }

    TriggerTransition();
    return true;
}

std::unique_ptr<Object3d> GimmickStageGate::Clone() const {
    // 複製先の描画資源を初期化した後で、ゲート設定をコピーします。
    auto newObj = std::make_unique<GimmickStageGate>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

int GimmickStageGate::GetStageIndex() const {
    // ステージ番号はシーンの Target ID と共有します。
    return GetTargetID();
}

int GimmickStageGate::GetGateMode() const {
    // 古い配置データにモードがなくても、遷移しない展示ノードとして扱います。
    if (!param_.has_value()) {
        return kGateModeStageSelectNode;
    }
    return param_->actionMode;
}

const std::string& GimmickStageGate::GetTargetSceneName() const {
    // 遷移先が未設定ならステージ選択画面へ戻します。
    static const std::string kDefaultScene = "SELECT";
    if (!param_.has_value() || param_->targetScene.empty()) {
        return kDefaultScene;
    }
    return param_->targetScene;
}

bool GimmickStageGate::IsStageSelectNodeMode() const {
    // 展示ノードと実際の遷移ゲートを呼び出し側で区別します。
    return GetGateMode() == kGateModeStageSelectNode;
}

void GimmickStageGate::SetGateState(bool selected, bool unlocked, bool cleared, bool unlocking) {
    // 選択・解放・クリア状態は Update の色と発光量へまとめて反映します。
    isSelected_ = selected;
    isUnlocked_ = unlocked;
    isCleared_ = cleared;
    isUnlocking_ = unlocking;
}

void GimmickStageGate::SetGateActivation(float activation) {
    // 呼び出し側の入力を正規化し、現在値には Update で追従させます。
    targetActivation_ = Clamp01(activation);
}

void GimmickStageGate::SetTransitionEnabled(bool enabled) {
    // 再有効化時は一度きりの遷移記録を戻し、再入場を許可します。
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
    // 専用材質がない複製・編集中は、シェーダーパラメータへのアクセスを避けます。
    auto* renderer = GetMeshRenderer();
    if (!renderer || !renderer->GetWaterParamData()) {
        return;
    }

    // 水系の共通パラメータをゲートの波・発光表現にも利用します。
    auto* portal = renderer->GetWaterParamData();
    const float active = isUnlocking_ ? 1.0f : (isUnlocked_ ? activation_ : 0.0f);
    const float selectedBoost = (isSelected_ && isUnlocked_) ? 1.0f : 0.0f;
    const float entryBurst = isUnlocked_ ? activationBurst_ : 0.0f;

    portal->waveSpeed = Lerp(0.28f, 2.25f, active) + entryBurst * 2.30f;
    portal->waveHeight = Lerp(0.18f, 1.35f, active) + entryBurst * 0.72f;
    portal->waveFrequency = Lerp(4.0f, 22.0f, active) + entryBurst * 9.0f;
    portal->effectSoftness = Lerp(0.28f, 0.66f, active) + entryBurst * 0.10f;
    portal->effectIntensity =
        isUnlocked_ ? Lerp(0.34f, 1.58f + selectedBoost * 0.44f, active) + entryBurst * 1.85f : 0.12f;
    portal->effectScale = Lerp(0.78f, 1.18f, active) + entryBurst * 0.18f;
    portal->effectType = isCleared_ ? 2.0f : 1.0f;
    portal->flowSpeedX = 0.0f;
    portal->flowSpeedY = 0.0f;
    portal->uvOffsetX = activationBurst_;
    portal->uvOffsetY = active;
}

// 見た目の開放状態と実際に遷移できる条件を分け、演出途中の誤入場を防ぎます。
bool GimmickStageGate::CanTriggerTransition() const {
    // 実際の遷移はモード、有効状態、遷移済みフラグで判定します。
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
    // 衝突が続くフレームでも ChangeScene を一度だけ要求します。
    hasTriggeredTransition_ = true;

    if (GetGateMode() == kGateModeStageTransition) {
        StageManager::GetInstance()->SetCurrentStage(GetStageIndex());
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        return;
    }

    SceneManager::GetInstance()->ChangeScene(GetTargetSceneName());
}

void GimmickStageGate::CaptureBaseScale() {
    // 編集時の元スケールを初回だけ保存し、演出による変化の基準にします。
    if (hasBaseScale_) {
        return;
    }
    baseScale_ = GetScale();
    hasBaseScale_ = true;
}

Vector4 GimmickStageGate::GetTargetColor() const {
    // 開放・クリア・選択の優先順位で色を決めます。
    if (!isUnlocked_) {
        // 未解放でもゲートの輪郭は残し、鎖と南京錠で封鎖状態を示します。
        return {0.30f, 0.54f, 0.64f, 0.92f};
    }
    const float active = isUnlocking_ ? 1.0f : activation_;
    if (isUnlocking_) {
        return {1.0f, 0.72f, 0.28f, 1.0f};
    }
    if (isCleared_) {
        const float alpha = Lerp(0.70f, 0.98f, active);
        if (isSelected_) {
            return {0.48f, 0.96f, 1.0f, alpha};
        }
        return {0.28f, 0.82f, 1.0f, alpha};
    }
    if (isSelected_) {
        return {1.0f, 0.62f, 0.24f, Lerp(0.78f, 1.0f, active)};
    }
    return {1.0f, 0.52f, 0.20f, Lerp(0.68f, 0.94f, active)};
}

#define NOMINMAX
#include "GimmickBossGate.h"

#include "CollisionConfig.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>

namespace {
float SmoothStep01(float value) {
    // 閉鎖率の両端で門の上下移動が急停止しないようにする。
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

void GimmickBossGate::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 閉鎖中の門を壁として扱い、開く方向と所要時間の既定値を用意する。
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("BossGate");
    SetName("Gimmick_BossGate");
    SetStatic(false);
    SetCastShadow(true);
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(0xffffffffu);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = {0.0f, 3.5f, 0.0f};
    collider.size = {6.0f, 3.5f, 0.55f};
    SetColliderConfig(collider);

    if (!param_.has_value())
        param_.emplace();
    param_->gimmickType = "BossGate";
    param_->moveSpeed = 0.58f;
    param_->moveAmount = 8.0f;
    param_->startActive = false;
    param_->returnOnOff = true;
}

void GimmickBossGate::Update(float deltaTime) {
    // エディターでは配置位置へ戻し、プレイ中のみ閉鎖率に応じて門を動かします。
    SceneManager* sceneManager = SceneManager::GetInstance();
    const bool isPlaying = sceneManager && sceneManager->IsPlaying();
    if (!isPlaying) {
        ResetForEditor();
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (cinematicOverride_) {
        SetCollisionEnabled(false);
        BaseGimmick::Update(deltaTime);
        return;
    }
    CaptureAuthoredState();
    if (!initializedForPlay_) {
        initializedForPlay_ = true;
        targetClosure_ = activationRequested_ || (param_.has_value() && param_->startActive) ? 1.0f : 0.0f;
        closure_ = targetClosure_;
        ApplyRuntimeState();
    }

    const float duration = GetTransitionDuration();
    const float step = duration > 0.0001f ? std::max(0.0f, deltaTime) / duration : 1.0f;
    if (closure_ < targetClosure_) {
        closure_ = std::min(targetClosure_, closure_ + step);
    } else if (closure_ > targetClosure_) {
        closure_ = std::max(targetClosure_, closure_ - step);
    }
    ApplyRuntimeState();
    BaseGimmick::Update(deltaTime);
}

void GimmickBossGate::OnTrigger() {
    // イベントからの起動はスイッチ ON と同じ閉鎖経路へ渡す。
    OnSwitchEvent(true);
}

void GimmickBossGate::OnSwitchEvent(bool active) {
    // シネマティック制御中は外部スイッチで門を上書きしない。
    if (cinematicOverride_)
        return;
    if (!active && param_.has_value() && !param_->returnOnOff) {
        return;
    }
    CaptureAuthoredState();
    activationRequested_ = active;
    targetClosure_ = active ? 1.0f : 0.0f;
    if (active) {
        SetIsVisible(true);
        if (closure_ <= 0.001f) {
            Vector3 cue = authoredPosition_;
            cue.y += 0.1f;
            VFXSequencer::PlayOneShot("high_crown_gate_close_cue", cue, {1.0f, 1.0f, 1.0f});
        }
    } else {
        SetCollisionEnabled(false);
    }
}

std::unique_ptr<Object3d> GimmickBossGate::Clone() const {
    // 編集配置を複製し、実行時の閉鎖率は新しい門で計算する。
    auto clone = std::make_unique<GimmickBossGate>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

Vector3 GimmickBossGate::GetClosedPosition() {
    // 閉鎖位置の基準は現在の編集配置から一度だけ確定する。
    CaptureAuthoredState();
    return authoredPosition_;
}

void GimmickBossGate::CaptureAuthoredState(bool force) {
    // 実行時の移動や衝突切替で、編集時の Transform とマスクを失わないよう保存する。
    if (authoredStateCaptured_ && !force) {
        return;
    }
    authoredPosition_ = GetTranslate();
    authoredScale_ = GetScale();
    authoredCollisionAttribute_ = GetCollisionAttribute();
    authoredCollisionMask_ = GetCollisionMask();
    if (authoredCollisionAttribute_ == 0)
        authoredCollisionAttribute_ = CollisionCategories::Get("ground");
    if (authoredCollisionMask_ == 0)
        authoredCollisionMask_ = 0xffffffffu;
    authoredStateCaptured_ = true;
}

void GimmickBossGate::ResetForEditor() {
    // 再生停止時は演出上書きと閉鎖率を消し、編集で決めた位置を復元する。
    cinematicOverride_ = false;
    if (initializedForPlay_) {
        SetTranslate(authoredPosition_);
        SetScale(authoredScale_);
    }
    CaptureAuthoredState(true);
    initializedForPlay_ = false;
    activationRequested_ = false;
    closure_ = 0.0f;
    targetClosure_ = 0.0f;
    SetTranslate(authoredPosition_);
    SetScale(authoredScale_);
    SetIsVisible(true);
    SetCollisionEnabled(false);
}

void GimmickBossGate::ApplyRuntimeState() {
    // 閉鎖率に応じて門を上下させ、十分閉じるまで壁の接触を有効にしない。
    const float eased = SmoothStep01(closure_);
    Vector3 position = authoredPosition_;
    position.y += (1.0f - eased) * GetTravelDistance();
    SetTranslate(position);
    SetScale(authoredScale_);
    SetIsVisible(closure_ > 0.001f || targetClosure_ > 0.0f);
    SetCollisionEnabled(targetClosure_ > 0.5f && closure_ >= 0.82f);
    if (closure_ <= 0.001f && targetClosure_ <= 0.0f) {
        SetIsVisible(false);
    }
}

void GimmickBossGate::SetCollisionEnabled(bool enabled) {
    // 編集時の属性・マスクを復元する形で壁判定を切り替える。
    SetCollisionAttribute(enabled ? authoredCollisionAttribute_ : 0);
    SetCollisionMask(enabled ? authoredCollisionMask_ : 0);
}

float GimmickBossGate::GetTransitionDuration() const {
    // JSON の移動速度欄を演出の所要秒数として安全範囲に収める。
    return param_.has_value() ? std::clamp(param_->moveSpeed, 0.12f, 2.5f) : 0.58f;
}

float GimmickBossGate::GetTravelDistance() const {
    // 門が完全に開く際の上昇距離に下限を設ける。
    return param_.has_value() ? std::max(2.0f, param_->moveAmount) : 8.0f;
}

#define NOMINMAX
#include "GimmickBossGate.h"
#include "Easing.h"
#include "ObjectPropertyDefinition.h"

#include "CollisionConfig.h"
#include "SceneManager.h"
#include "VfxSequencer.h"

#include <algorithm>
#include <cassert>

namespace {

} // namespace

void GimmickBossGate::Initialize(engine::Object3DCommon* common, const std::string& modelName) {
    // 閉鎖中の門を壁として扱い、開く方向と所要時間の既定値を用意する。
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("BossGate");
    SetName("Gimmick_BossGate");
    SetStatic(false);
    SetCastShadow(true);
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(0xffffffffu);

    engine::Object3D::ColliderConfig collider;
    collider.type = ColliderType::Obb;
    collider.center = {0.0f, 3.5f, 0.0f};
    collider.size = {6.0f, 3.5f, 0.55f};
    SetColliderConfig(collider);

    if (!GetParameters().has_value())
        ResetParameters();
    SetParameter(&engine::EntityParameter::gimmickType, "BossGate");
    SetParameter(&engine::EntityParameter::moveSpeed, 0.58f);
    SetParameter(&engine::EntityParameter::moveAmount, 8.0f);
    SetParameter(&engine::EntityParameter::startActive, false);
    SetParameter(&engine::EntityParameter::returnOnOff, true);

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickBossGate;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "ボス闘技場・格子ゲート",
        "Transformは閉鎖位置です。演出中はタイムライン制御、通常時はイベント通知で開閉します。");
    using Parameter = engine::Object3D::EntityParameter;
    properties.Parameter("開門時の退避距離", "moveAmount", &Parameter::moveAmount, 0.0f, 100.0f);
    properties.Parameter("開閉にかかる時間", "moveSpeed", &Parameter::moveSpeed, 0.05f, 10.0f);
    properties.Parameter("開始時に有効", "startActive", &Parameter::startActive);
    properties.Parameter("OFFで元に戻す", "returnOnOff", &Parameter::returnOnOff);
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
        if (introTimelineControl_) {
            // 見た目の移動はTimelineが担当し、接触判定だけ閉鎖位置への到達で決めます。
            const float remaining = GetTranslate().y - authoredPosition_.y;
            SetCollisionEnabled(targetClosure_ > 0.5f && remaining <= GetTravelDistance() * 0.18f);
        } else {
            SetCollisionEnabled(false);
        }
        BaseGimmick::Update(deltaTime);
        return;
    }

    CaptureAuthoredState();

    if (!initializedForPlay_) {
        initializedForPlay_ = true;
        targetClosure_ = activationRequested_ || (GetParameters().has_value() && GetParameters()->startActive) ? 1.0f : 0.0f;
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
    if (!active && GetParameters().has_value() && !GetParameters()->returnOnOff) {
        return;
    }

    CaptureAuthoredState();
    activationRequested_ = active;
    targetClosure_ = active ? 1.0f : 0.0f;

    if (active) {
        SetIsVisible(true);

        if (closure_ <= 0.001f) {
            engine::Vector3 cue = authoredPosition_;
            cue.y += 0.1f;
            VfxSequencer::PlayOneShot("high_crown_gate_close_cue", cue, {1.0f, 1.0f, 1.0f});
        }
    } else {
        SetCollisionEnabled(false);
    }
}

std::unique_ptr<engine::Object3D> GimmickBossGate::Clone() const {
    // 編集配置を複製し、実行時の閉鎖率は新しい門で計算する。
    auto clone = std::make_unique<GimmickBossGate>();
    assert(GetObjectCommon() != nullptr);
    clone->Initialize(GetObjectCommon(), GetModelName());
    clone->CopyFrom(this);
    return clone;
}

engine::Vector3 GimmickBossGate::GetClosedPosition() {
    // 閉鎖位置の基準は現在の編集配置から一度だけ確定する。
    CaptureAuthoredState();
    return authoredPosition_;
}

void GimmickBossGate::SetIntroTimelineControl(bool enabled) {
    CaptureAuthoredState();
    introTimelineControl_ = enabled;
    cinematicOverride_ = enabled;

    if (!enabled) {
        // Timelineが初期姿勢へ復元した後も、封鎖済みの進行状態を保持します。
        closure_ = targetClosure_;
        ApplyRuntimeState();
    }
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
    introTimelineControl_ = false;

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
    const float eased = Easing::SmoothStep01(closure_);
    engine::Vector3 position = authoredPosition_;
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
    return GetParameters().has_value() ? std::clamp(GetParameters()->moveSpeed, 0.12f, 2.5f) : 0.58f;
}

float GimmickBossGate::GetTravelDistance() const {
    // 門が完全に開く際の上昇距離に下限を設ける。
    return GetParameters().has_value() ? std::max(2.0f, GetParameters()->moveAmount) : 8.0f;
}

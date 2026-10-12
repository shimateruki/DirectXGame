#include "GimmickEventReceiver.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickEventReceiver::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // イベントを受けた際に表示・移動する地形なので、通常時の地面属性を保持します。
    SetClassName("Gimmick");
    SetGimmickType("EventReceiver");
    SetName("Gimmick_EventReceiver");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetColor({0.65f, 1.0f, 0.65f, 1.0f});
    SetStatic(false);

    if (!param_.has_value())
        param_.emplace();
}

void GimmickEventReceiver::Update(float deltaTime) {
    // 編集時の配置を再生基準にし、ボス報酬はイベント発火まで隠します。
    if (!param_.has_value())
        param_.emplace();

    bool isPlaying = false;
    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        initializedForPlay_ = false;
        hasPendingActive_ = false;
        const bool isBossReward = param_->actionMode == 6;
        SetIsVisible(!isBossReward);
        SetCollisionEnabled(!isBossReward);
        BaseGimmick::Update(deltaTime);
        return;
    }

    // 最初の再生フレームで元の見た目と衝突設定を記録します。
    if (!initializedForPlay_) {
        basePosition_ = GetTransform()->translate;
        targetPosition_ = basePosition_;
        baseScale_ = GetScale();
        baseRotation_ = GetRotation();
        originalCollisionAttribute_ = GetCollisionAttribute();
        originalCollisionMask_ = GetCollisionMask();
        originalColor_ = GetColor();
        if (originalCollisionAttribute_ == 0)
            originalCollisionAttribute_ = CollisionCategories::Get("ground");
        if (originalCollisionMask_ == 0)
            originalCollisionMask_ = CollisionCategories::GetDefaultGameplayMask();

        active_ = hasPendingActive_ ? pendingActive_ : param_->startActive;
        hasPendingActive_ = false;
        ApplyActiveState(active_);
        initializedForPlay_ = true;
    }

    // 軸移動モードと報酬の出現演出は同じ受信オブジェクトで処理します。
    int actionMode = param_->actionMode;
    if (actionMode >= 1 && actionMode <= 3) {
        float speed = (std::max)(0.01f, param_->moveSpeed);
        float t = 1.0f - std::exp(-speed * deltaTime);
        Vector3 pos = GetTransform()->translate;
        pos.x = Math::Lerp(pos.x, targetPosition_.x, t);
        pos.y = Math::Lerp(pos.y, targetPosition_.y, t);
        pos.z = Math::Lerp(pos.z, targetPosition_.z, t);
        SetTranslate(pos);
    } else if (actionMode == 6 && active_) {
        activationTimer_ += (std::max)(0.0f, deltaTime);
        const float duration = (std::max)(0.35f, param_->fallDuration);
        const float progress = (std::clamp)(activationTimer_ / duration, 0.0f, 1.0f);
        const float eased = progress * progress * (3.0f - 2.0f * progress);
        const Vector3 startPosition = basePosition_ + Vector3{0.0f, 4.05f, -param_->moveAmount};
        Vector3 position = startPosition * (1.0f - eased) + basePosition_ * eased;
        position.y += std::sin(progress * 3.1415926535f) * (std::max)(0.0f, param_->jumpPower);
        SetTranslate(position);

        Vector3 rotation = baseRotation_;
        rotation.y += activationTimer_ * param_->moveSpeed;
        rotation.z += std::sin(progress * 3.1415926535f) * 0.52f;
        SetRotation(rotation);

        const float startScale = 1.55f;
        const float scaleRate = startScale + (1.0f - startScale) * eased;
        SetScale(baseScale_ * scaleRate);
        SetCollisionEnabled(progress >= 1.0f);
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickEventReceiver::RelocateRuntimeBase(const Vector3& position) {
    // 実行中の相対移動量を保ったまま、演出全体の基準位置だけを移します。
    const Vector3 offset = position - (initializedForPlay_ ? basePosition_ : GetTranslate());
    basePosition_ = position;
    targetPosition_ = initializedForPlay_ ? targetPosition_ + offset : position;
    SetTranslate(GetTranslate() + offset);
    UpdateWorldMatrix(true);
}

void GimmickEventReceiver::OnTrigger() {
    // 単発トリガーはスイッチのオンイベントとして扱います。
    OnSwitchEvent(true);
}

void GimmickEventReceiver::OnSwitchEvent(bool active) {
    // 再生開始前に届いたイベントも、基準姿勢の取得後に適用できるよう保留します。
    if (!param_.has_value())
        param_.emplace();

    if (!active && !param_->returnOnOff)
        return;
    if (!initializedForPlay_) {
        pendingActive_ = active;
        hasPendingActive_ = true;
        return;
    }
    ApplyActiveState(active);
}

void GimmickEventReceiver::ApplyActiveState(bool active) {
    // モード別に表示・衝突・移動先を切り替え、報酬出現時は着地まで衝突を遅らせます。
    active_ = active;
    activationTimer_ = 0.0f;
    int actionMode = param_.has_value() ? param_->actionMode : 0;
    float moveAmount = param_.has_value() ? param_->moveAmount : 10.0f;

    if (actionMode == 0) {
        SetIsVisible(active);
        SetCollisionEnabled(active);
        // エディタで設定した色を尊重し、報酬王冠などを汎用緑色へ上書きしません。
        SetColor(originalColor_);
    } else if (actionMode >= 1 && actionMode <= 3) {
        Vector3 offset{};
        if (actionMode == 1)
            offset.y = moveAmount;
        if (actionMode == 2)
            offset.x = moveAmount;
        if (actionMode == 3)
            offset.z = moveAmount;
        targetPosition_ = active ? basePosition_ + offset : basePosition_;
        SetIsVisible(true);
        SetCollisionEnabled(true);
    } else if (actionMode == 4) {
        SetIsVisible(active);
        SetCollisionEnabled(active);
    } else if (actionMode == 5) {
        SetIsVisible(!active);
        SetCollisionEnabled(!active);
    } else if (actionMode == 6) {
        SetScale(baseScale_);
        SetRotation(baseRotation_);
        if (active) {
            SetTranslate(basePosition_ + Vector3{0.0f, 4.05f, -moveAmount});
            SetIsVisible(true);
            SetCollisionEnabled(false);
        } else {
            SetTranslate(basePosition_);
            SetIsVisible(false);
            SetCollisionEnabled(false);
        }
    }
}

void GimmickEventReceiver::SetCollisionEnabled(bool enabled) {
    // 有効化時は編集データで指定された属性とマスクを復元します。
    if (enabled) {
        SetCollisionAttribute(originalCollisionAttribute_ != 0 ? originalCollisionAttribute_ : CollisionCategories::Get("ground"));
        SetCollisionMask(originalCollisionMask_ != 0 ? originalCollisionMask_ : CollisionCategories::GetDefaultGameplayMask());
    } else {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
    }
}

std::unique_ptr<Object3d> GimmickEventReceiver::Clone() const {
    // 複製先を初期化してから、イベント設定と配置をコピーします。
    auto newObj = std::make_unique<GimmickEventReceiver>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

#include "GimmickAppearingFloor.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickAppearingFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 表示と当たり判定を同時に切り替える床として登録する。
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("AppearingFloor");
    SetName("Gimmick_AppearingFloor");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetColor({0.55f, 1.0f, 0.7f, 1.0f});
    SetStatic(false);
}

// Play中だけタイマーを進め、Editorでは足場を常に見える状態に戻します。
void GimmickAppearingFloor::Update(float deltaTime) {
    if (param_.has_value()) {
        appearDuration_ = param_->interval;
        if (appearDuration_ <= 0.0f)
            appearDuration_ = 3.0f;
    }

    bool isPlaying = false;
    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        initializedForPlay_ = false;
        SetFloorActive(true);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        // 初回だけEditorで設定された衝突属性を退避し、待機中の当たり判定を外します。
        originalCollisionAttribute_ = GetCollisionAttribute();
        originalCollisionMask_ = GetCollisionMask();
        if (originalCollisionAttribute_ == 0)
            originalCollisionAttribute_ = CollisionCategories::Get("ground");
        if (originalCollisionMask_ == 0)
            originalCollisionMask_ = CollisionCategories::GetDefaultGameplayMask();

        visibleTimer_ = 0.0f;
        SetFloorActive(false);
        initializedForPlay_ = true;
    }

    if (visibleTimer_ > 0.0f) {
        visibleTimer_ -= deltaTime;
        if (visibleTimer_ < 0.0f)
            visibleTimer_ = 0.0f;
    }

    bool shouldBeActive = visibleTimer_ > 0.0f;
    SetFloorActive(shouldBeActive);

    if (shouldBeActive) {
        // 消滅直前に点滅させ、プレイヤーに残り時間を知らせます。
        float warningTime = (std::min)(blinkWarningTime_, appearDuration_ * 0.5f);
        if (visibleTimer_ <= warningTime) {
            bool blinkOn = std::fmod(visibleTimer_, blinkInterval_ * 2.0f) >= blinkInterval_;
            SetIsVisible(blinkOn);
            SetColor(blinkOn ? Vector4{0.9f, 1.0f, 0.35f, 1.0f} : Vector4{0.55f, 1.0f, 0.7f, 1.0f});
        } else {
            SetIsVisible(true);
            SetColor({0.55f, 1.0f, 0.7f, 1.0f});
        }
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickAppearingFloor::OnTrigger() {
    // 再起動時は表示時間を延長し、床を再び有効にする。
    visibleTimer_ = appearDuration_;
    SetFloorActive(true);
    SetIsVisible(true);
}

void GimmickAppearingFloor::SetFloorActive(bool active) {
    // 見た目と床としての接触状態を同じフラグへそろえる。
    if (isActive_ == active && GetIsVisible() == active)
        return;

    isActive_ = active;
    SetIsVisible(active);

    if (active) {
        SetCollisionAttribute(originalCollisionAttribute_ != 0 ? originalCollisionAttribute_ : CollisionCategories::Get("ground"));
        SetCollisionMask(originalCollisionMask_ != 0 ? originalCollisionMask_ : CollisionCategories::GetDefaultGameplayMask());
        SetColor({0.55f, 1.0f, 0.7f, 1.0f});
    } else {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
    }
}

std::unique_ptr<Object3d> GimmickAppearingFloor::Clone() const {
    // 新しい実体を初期化してから、出現時間と配置を引き継ぐ。
    auto newObj = std::make_unique<GimmickAppearingFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

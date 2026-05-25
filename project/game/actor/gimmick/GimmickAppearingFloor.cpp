#include "GimmickAppearingFloor.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickAppearingFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("AppearingFloor");
    SetName("Gimmick_AppearingFloor");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetColor({ 0.55f, 1.0f, 0.7f, 1.0f });
    SetStatic(false);
}

void GimmickAppearingFloor::Update(float deltaTime) {
    if (param_.has_value()) {
        appearDuration_ = param_->interval;
        if (appearDuration_ <= 0.0f) appearDuration_ = 3.0f;
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
        originalCollisionAttribute_ = GetCollisionAttribute();
        originalCollisionMask_ = GetCollisionMask();
        if (originalCollisionAttribute_ == 0) originalCollisionAttribute_ = kGround;
        if (originalCollisionMask_ == 0) originalCollisionMask_ = 0b11111111;

        visibleTimer_ = 0.0f;
        SetFloorActive(false);
        initializedForPlay_ = true;
    }

    if (visibleTimer_ > 0.0f) {
        visibleTimer_ -= deltaTime;
        if (visibleTimer_ < 0.0f) visibleTimer_ = 0.0f;
    }

    bool shouldBeActive = visibleTimer_ > 0.0f;
    SetFloorActive(shouldBeActive);

    if (shouldBeActive) {
        float warningTime = (std::min)(blinkWarningTime_, appearDuration_ * 0.5f);
        if (visibleTimer_ <= warningTime) {
            bool blinkOn = std::fmod(visibleTimer_, blinkInterval_ * 2.0f) >= blinkInterval_;
            SetIsVisible(blinkOn);
            SetColor(blinkOn ? Vector4{ 0.9f, 1.0f, 0.35f, 1.0f } : Vector4{ 0.55f, 1.0f, 0.7f, 1.0f });
        }
        else {
            SetIsVisible(true);
            SetColor({ 0.55f, 1.0f, 0.7f, 1.0f });
        }
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickAppearingFloor::OnTrigger() {
    visibleTimer_ = appearDuration_;
    SetFloorActive(true);
    SetIsVisible(true);
}

void GimmickAppearingFloor::SetFloorActive(bool active) {
    if (isActive_ == active && GetIsVisible() == active) return;

    isActive_ = active;
    SetIsVisible(active);

    if (active) {
        SetCollisionAttribute(originalCollisionAttribute_ != 0 ? originalCollisionAttribute_ : kGround);
        SetCollisionMask(originalCollisionMask_ != 0 ? originalCollisionMask_ : 0b11111111);
        SetColor({ 0.55f, 1.0f, 0.7f, 1.0f });
    }
    else {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
    }
}

std::unique_ptr<Object3d> GimmickAppearingFloor::Clone() const {
    auto newObj = std::make_unique<GimmickAppearingFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

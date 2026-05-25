#include "GimmickEventReceiver.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickEventReceiver::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("EventReceiver");
    SetName("Gimmick_EventReceiver");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetColor({ 0.65f, 1.0f, 0.65f, 1.0f });
    SetStatic(false);

    if (!param_.has_value()) param_.emplace();
}

void GimmickEventReceiver::Update(float deltaTime) {
    if (!param_.has_value()) param_.emplace();

    bool isPlaying = false;
    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        initializedForPlay_ = false;
        SetIsVisible(true);
        SetCollisionEnabled(true);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        basePosition_ = GetTransform()->translate;
        targetPosition_ = basePosition_;
        originalCollisionAttribute_ = GetCollisionAttribute();
        originalCollisionMask_ = GetCollisionMask();
        if (originalCollisionAttribute_ == 0) originalCollisionAttribute_ = kGround;
        if (originalCollisionMask_ == 0) originalCollisionMask_ = 0b11111111;

        active_ = param_->startActive;
        ApplyActiveState(active_);
        initializedForPlay_ = true;
    }

    int actionMode = param_->actionMode;
    if (actionMode >= 1 && actionMode <= 3) {
        float speed = (std::max)(0.01f, param_->moveSpeed);
        float t = 1.0f - std::exp(-speed * deltaTime);
        Vector3 pos = GetTransform()->translate;
        pos.x = Math::Lerp(pos.x, targetPosition_.x, t);
        pos.y = Math::Lerp(pos.y, targetPosition_.y, t);
        pos.z = Math::Lerp(pos.z, targetPosition_.z, t);
        GetTransform()->translate = pos;
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickEventReceiver::OnTrigger() {
    OnSwitchEvent(true);
}

void GimmickEventReceiver::OnSwitchEvent(bool active) {
    if (!param_.has_value()) param_.emplace();

    if (!active && !param_->returnOnOff) return;
    ApplyActiveState(active);
}

void GimmickEventReceiver::ApplyActiveState(bool active) {
    active_ = active;
    int actionMode = param_.has_value() ? param_->actionMode : 0;
    float moveAmount = param_.has_value() ? param_->moveAmount : 10.0f;

    if (actionMode == 0) {
        SetIsVisible(active);
        SetCollisionEnabled(active);
        SetColor({ 0.55f, 1.0f, 0.7f, 1.0f });
    }
    else if (actionMode >= 1 && actionMode <= 3) {
        Vector3 offset{};
        if (actionMode == 1) offset.y = moveAmount;
        if (actionMode == 2) offset.x = moveAmount;
        if (actionMode == 3) offset.z = moveAmount;
        targetPosition_ = active ? basePosition_ + offset : basePosition_;
        SetIsVisible(true);
        SetCollisionEnabled(true);
    }
    else if (actionMode == 4) {
        SetIsVisible(active);
        SetCollisionEnabled(active);
    }
    else if (actionMode == 5) {
        SetIsVisible(!active);
        SetCollisionEnabled(!active);
    }
}

void GimmickEventReceiver::SetCollisionEnabled(bool enabled) {
    if (enabled) {
        SetCollisionAttribute(originalCollisionAttribute_ != 0 ? originalCollisionAttribute_ : kGround);
        SetCollisionMask(originalCollisionMask_ != 0 ? originalCollisionMask_ : 0b11111111);
    }
    else {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
    }
}

std::unique_ptr<Object3d> GimmickEventReceiver::Clone() const {
    auto newObj = std::make_unique<GimmickEventReceiver>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

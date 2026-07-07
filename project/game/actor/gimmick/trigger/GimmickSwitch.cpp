#include "GimmickSwitch.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <cmath>

void GimmickSwitch::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("Switch");
    SetName("Gimmick_Switch");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
    SetScale({ 1.5f, 0.25f, 1.5f });

    if (!param_.has_value()) param_.emplace();
}

void GimmickSwitch::Update(float deltaTime) {
    if (!param_.has_value()) param_.emplace();

    if (contactTimer_ > 0.0f) {
        contactTimer_ -= deltaTime;
        if (contactTimer_ < 0.0f) contactTimer_ = 0.0f;
    }

    bool isPressed = contactTimer_ > 0.0f;
    bool pressedThisFrame = isPressed && !wasPressed_;
    int mode = GetSwitchMode();

    if (mode == 0) {
        SendActive(isPressed);
    }
    else if (mode == 1) {
        if (pressedThisFrame) {
            SendActive(!isOutputActive_);
        }
    }
    else {
        if (pressedThisFrame) {
            timedTimer_ = param_->interval > 0.0f ? param_->interval : 3.0f;
            SendActive(true);
        }

        if (timedTimer_ > 0.0f) {
            timedTimer_ -= deltaTime;
            if (timedTimer_ <= 0.0f) {
                timedTimer_ = 0.0f;
                SendActive(false);
            }
        }
    }

    wasPressed_ = isPressed;

    if (isOutputActive_) {
        float pulse = 0.78f + std::sin(timedTimer_ * 10.0f + contactTimer_ * 30.0f) * 0.12f;
        SetColor({ 0.2f, 1.0f, 0.4f + pulse * 0.25f, 1.0f });
    }
    else if (isPressed) {
        SetColor({ 0.35f, 0.95f, 1.0f, 1.0f });
    }
    else {
        SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
    }

    BaseGimmick::Update(deltaTime);
}

bool GimmickSwitch::OnCollision(Object3d* other) {
    if (!other || other->GetClassName() != "Player") return true;

    CollisionInfo info = CheckCollision(other);
    if (info.isColliding && info.normal.y < -0.5f) {
        contactTimer_ = 0.12f;
    }

    return true;
}

void GimmickSwitch::SendActive(bool active) {
    if (isOutputActive_ == active) return;

    isOutputActive_ = active;
    int targetID = GetTargetID();
    if (targetID == -1) return;

    if (BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
        scene->SetEventActive(targetID, active);
    }
}

int GimmickSwitch::GetSwitchMode() const {
    return param_.has_value() ? param_->switchMode : 0;
}

std::unique_ptr<Object3d> GimmickSwitch::Clone() const {
    auto newObj = std::make_unique<GimmickSwitch>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

#include "GimmickTimedSwitch.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <cmath>

void GimmickTimedSwitch::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("TimedSwitch");
    SetName("Gimmick_TimedSwitch");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
    SetScale({ 1.5f, 0.25f, 1.5f });
}

void GimmickTimedSwitch::Update(float deltaTime) {
    if (contactTimer_ > 0.0f) {
        contactTimer_ -= deltaTime;
        if (contactTimer_ < 0.0f) contactTimer_ = 0.0f;
    }

    bool isPressed = contactTimer_ > 0.0f;
    if (isPressed) {
        float pulse = 0.85f + std::sin(contactTimer_ * 35.0f) * 0.15f;
        SetColor({ 0.2f, 1.0f, 0.45f + pulse * 0.2f, 1.0f });
    }
    else {
        SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
    }

    BaseGimmick::Update(deltaTime);
}

bool GimmickTimedSwitch::OnCollision(Object3d* other) {
    if (!other || other->GetClassName() != "Player") return true;

    CollisionInfo info = CheckCollision(other);
    if (info.isColliding && info.normal.y < -0.5f) {
        contactTimer_ = 0.12f;

        int targetID = GetTargetID();
        if (targetID != -1) {
            if (BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
                scene->TriggerEvent(targetID);
            }
        }
    }

    return true;
}

std::unique_ptr<Object3d> GimmickTimedSwitch::Clone() const {
    auto newObj = std::make_unique<GimmickTimedSwitch>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

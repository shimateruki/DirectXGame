#include "GimmickRotatingObject.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <cmath>

void GimmickRotatingObject::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("RotatingFloor");
    SetName("Gimmick_RotatingFloor");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetColor({ 0.95f, 0.65f, 0.35f, 1.0f });
    SetScale({ 3.0f, 0.3f, 1.2f });
    SetStatic(false);

    if (!param_.has_value()) param_.emplace();
    param_->speed = 45.0f;
    param_->actionMode = 1;
    param_->startActive = true;
    param_->returnOnOff = true;
}

void GimmickRotatingObject::Update(float deltaTime) {
    if (!param_.has_value()) param_.emplace();

    bool isPlaying = false;
    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        initializedForPlay_ = false;
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        active_ = param_->startActive;
        initializedForPlay_ = true;
    }

    if (active_) {
        const float radiansPerSecond = param_->speed * 3.14159265f / 180.0f;
        Vector3 rot = GetTransform()->rotate;

        if (param_->actionMode == 0) {
            rot.x += radiansPerSecond * deltaTime;
        }
        else if (param_->actionMode == 2) {
            rot.z += radiansPerSecond * deltaTime;
        }
        else {
            rot.y += radiansPerSecond * deltaTime;
        }

        GetTransform()->rotate = rot;
        GetTransform()->isQuaternionMaster = false;
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickRotatingObject::OnTrigger() {
    OnSwitchEvent(true);
}

void GimmickRotatingObject::OnSwitchEvent(bool active) {
    if (!param_.has_value()) param_.emplace();
    if (!active && !param_->returnOnOff) return;
    active_ = active;
}

std::unique_ptr<Object3d> GimmickRotatingObject::Clone() const {
    auto newObj = std::make_unique<GimmickRotatingObject>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

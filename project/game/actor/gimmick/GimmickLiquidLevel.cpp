#include "GimmickLiquidLevel.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickLiquidLevel::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("LiquidLevel");
    SetName("Gimmick_LiquidLevel");
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetScale({ 4.0f, 0.08f, 4.0f });
    SetStatic(false);

    if (!param_.has_value()) param_.emplace();
    param_->moveAmount = 6.0f;
    param_->moveSpeed = 3.0f;
    param_->startActive = false;
    param_->returnOnOff = true;

    ApplyLiquidVisual();
}

void GimmickLiquidLevel::Update(float deltaTime) {
    if (!param_.has_value()) param_.emplace();

    bool isPlaying = false;
    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        initializedForPlay_ = false;
        ApplyLiquidVisual();
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        basePosition_ = GetTransform()->translate;
        targetPosition_ = basePosition_;
        active_ = param_->startActive;
        ApplyTarget(active_);
        initializedForPlay_ = true;
    }

    float speed = (std::max)(0.01f, param_->moveSpeed);
    float t = 1.0f - std::exp(-speed * deltaTime);
    Vector3 pos = GetTransform()->translate;
    pos.x = Math::Lerp(pos.x, targetPosition_.x, t);
    pos.y = Math::Lerp(pos.y, targetPosition_.y, t);
    pos.z = Math::Lerp(pos.z, targetPosition_.z, t);
    GetTransform()->translate = pos;

    ApplyLiquidVisual();
    BaseGimmick::Update(deltaTime);
}

void GimmickLiquidLevel::OnTrigger() {
    OnSwitchEvent(true);
}

void GimmickLiquidLevel::OnSwitchEvent(bool active) {
    if (!param_.has_value()) param_.emplace();
    if (!active && !param_->returnOnOff) return;
    ApplyTarget(active);
}

void GimmickLiquidLevel::ApplyTarget(bool active) {
    active_ = active;
    float amount = param_.has_value() ? param_->moveAmount : 6.0f;
    targetPosition_ = active ? basePosition_ + Vector3{ 0.0f, amount, 0.0f } : basePosition_;
}

void GimmickLiquidLevel::ApplyLiquidVisual() {
    int colorType = param_.has_value() ? param_->colorType : 0;
    if (colorType == 1) {
        SetMaterialType(9);
        SetColor({ 1.0f, 0.25f, 0.02f, 0.88f });
    }
    else {
        SetMaterialType(8);
        SetColor({ 0.45f, 0.85f, 1.0f, 0.65f });
    }
}

std::unique_ptr<Object3d> GimmickLiquidLevel::Clone() const {
    auto newObj = std::make_unique<GimmickLiquidLevel>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

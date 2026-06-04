#include "GimmickStageGate.h"
#include "CollisionConfig.h"
#include <cassert>
#include <cmath>

void GimmickStageGate::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("StageGate");
    SetName("Gimmick_StageGate");
    SetColor({ 0.35f, 0.75f, 1.0f, 1.0f });
    SetScale({ 1.4f, 1.4f, 1.4f });
    SetEmissive(1.2f);

    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(4.0f);
}

void GimmickStageGate::Update(float deltaTime) {
    CaptureBaseScale();

    pulseTimer_ += deltaTime;
    float pulse = 1.0f;
    if (isUnlocking_) {
        pulse += std::sin(pulseTimer_ * 8.0f) * 0.22f;
    }
    else if (isSelected_ && isUnlocked_) {
        pulse += std::sin(pulseTimer_ * 5.0f) * 0.12f;
    }
    else if (isUnlocked_) {
        pulse += std::sin(pulseTimer_ * 2.0f) * 0.04f;
    }

    SetScale({ baseScale_.x * pulse, baseScale_.y * pulse, baseScale_.z * pulse });
    SetColor(GetTargetColor());
    float emissive = isUnlocked_ ? 1.25f : 0.35f;
    if (isSelected_ && isUnlocked_) emissive = 2.4f;
    if (isCleared_) emissive = 1.8f;
    if (isUnlocking_) emissive = 4.0f + std::sin(pulseTimer_ * 10.0f) * 0.9f;
    SetEmissive(emissive);

    Transform* transform = GetTransform();
    transform->rotate.y += (isSelected_ ? 1.8f : 0.45f) * deltaTime;
    transform->isQuaternionMaster = false;

    BaseGimmick::Update(deltaTime);
}

bool GimmickStageGate::OnCollision(Object3d* other) {
    (void)other;
    return false;
}

std::unique_ptr<Object3d> GimmickStageGate::Clone() const {
    auto newObj = std::make_unique<GimmickStageGate>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

int GimmickStageGate::GetStageIndex() const {
    return GetTargetID();
}

void GimmickStageGate::SetGateState(bool selected, bool unlocked, bool cleared, bool unlocking) {
    isSelected_ = selected;
    isUnlocked_ = unlocked;
    isCleared_ = cleared;
    isUnlocking_ = unlocking;
}

void GimmickStageGate::CaptureBaseScale() {
    if (hasBaseScale_) {
        return;
    }
    baseScale_ = GetScale();
    hasBaseScale_ = true;
}

Vector4 GimmickStageGate::GetTargetColor() const {
    if (!isUnlocked_) {
        return { 0.18f, 0.18f, 0.22f, 0.85f };
    }
    if (isUnlocking_) {
        return { 1.0f, 0.82f, 0.22f, 1.0f };
    }
    if (isSelected_) {
        return { 1.0f, 0.86f, 0.2f, 1.0f };
    }
    if (isCleared_) {
        return { 0.35f, 1.0f, 0.58f, 1.0f };
    }
    return { 0.35f, 0.75f, 1.0f, 1.0f };
}

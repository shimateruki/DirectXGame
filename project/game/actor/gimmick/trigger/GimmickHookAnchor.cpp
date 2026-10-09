#include "GimmickHookAnchor.h"
#include "CollisionConfig.h"
#include <cmath>

void GimmickHookAnchor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("HookAnchor");
    SetName("Gimmick_HookAnchor");
    SetColor({0.2f, 0.85f, 1.0f, 1.0f});
    SetScale({1.2f, 1.2f, 1.2f});

    SetCollisionAttribute(CollisionCategories::Get("hook_anchor"));
    SetCollisionMask(CollisionCategories::Get("player"));
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(2.5f);
}

void GimmickHookAnchor::Update(float deltaTime) {
    pulseTimer_ += deltaTime;

    float pulse = 1.0f + std::sin(pulseTimer_ * 3.2f) * 0.08f;
    SetScale({1.2f * pulse, 1.2f * pulse, 1.2f * pulse});

    BaseGimmick::Update(deltaTime);
}

// フックの対象にはなりますが、接触そのものでは追加処理を行いません。
bool GimmickHookAnchor::OnCollision(Object3d* other) {
    (void)other;
    return false;
}

std::unique_ptr<Object3d> GimmickHookAnchor::Clone() const {
    auto newObj = std::make_unique<GimmickHookAnchor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

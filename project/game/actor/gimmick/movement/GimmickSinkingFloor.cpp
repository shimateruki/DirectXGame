#include "GimmickSinkingFloor.h"
#include "CollisionConfig.h"

void GimmickSinkingFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("SinkingFloor");
    SetName("Gimmick_SinkingFloor");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
}

void GimmickSinkingFloor::Update(float deltaTime) {
    if (!initializedStart_) {
        startPos_ = GetTransform()->translate;
        initializedStart_ = true;
    }

    if (contactTimer_ > 0.0f) {
        contactTimer_ -= deltaTime;
        if (contactTimer_ < 0.0f) contactTimer_ = 0.0f;
    }

    const bool isStepped = contactTimer_ > 0.0f;
    float targetY = startPos_.y + (isStepped ? -sinkDepth_ : 0.0f);
    float t = 1.0f - std::exp(-sinkSpeed_ * deltaTime);

    Vector3 pos = GetTransform()->translate;
    pos.y = Math::Lerp(pos.y, targetY, t);
    GetTransform()->translate = pos;

    BaseGimmick::Update(deltaTime);
}

bool GimmickSinkingFloor::OnCollision(Object3d* other) {
    if (other && other->GetClassName() == "Player") {
        CollisionInfo info = CheckCollision(other);
        if (info.isColliding && info.normal.y < -0.5f) {
            contactTimer_ = 0.12f;
        }
    }
    return true;
}

std::unique_ptr<Object3d> GimmickSinkingFloor::Clone() const {
    auto newObj = std::make_unique<GimmickSinkingFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

#include "GimmickOneWayFloor.h"
#include "CollisionConfig.h"
#include <cassert>

void GimmickOneWayFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("OneWayFloor");
    SetName("Gimmick_OneWayFloor");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetColor({ 0.85f, 0.9f, 0.65f, 0.9f });
    SetScale({ 2.5f, 0.22f, 2.5f });
}

bool GimmickOneWayFloor::OnCollision(Object3d* other) {
    (void)other;
    return true;
}

std::unique_ptr<Object3d> GimmickOneWayFloor::Clone() const {
    auto newObj = std::make_unique<GimmickOneWayFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

#include "GimmickIceFloor.h"
#include "CollisionConfig.h"
#include "Player.h"

void GimmickIceFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("IceFloor");
    SetName("Gimmick_IceFloor");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetColor({ 0.65f, 0.9f, 1.0f, 0.9f });
}

bool GimmickIceFloor::OnCollision(Object3d* other) {
    Player* player = dynamic_cast<Player*>(other);
    if (!player) return true;

    CollisionInfo info = CheckCollision(other);
    if (info.isColliding && info.normal.y < -0.5f) {
        player->ApplyIceSurface(0.18f, 0.22f, 0.22f);
    }

    return true;
}

std::unique_ptr<Object3d> GimmickIceFloor::Clone() const {
    auto newObj = std::make_unique<GimmickIceFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

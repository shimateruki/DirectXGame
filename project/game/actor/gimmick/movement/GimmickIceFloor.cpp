#include "GimmickIceFloor.h"
#include "CollisionConfig.h"
#include "Player.h"

void GimmickIceFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("IceFloor");
    SetName("Gimmick_IceFloor");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetColor({0.65f, 0.9f, 1.0f, 0.9f});
}

// 上面で接している間だけ氷床の摩擦・旋回補正を短時間ずつ延長します。
bool GimmickIceFloor::OnCollision(Object3d* other) {
    Player* player = dynamic_cast<Player*>(other);
    if (!player)
        return true;

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

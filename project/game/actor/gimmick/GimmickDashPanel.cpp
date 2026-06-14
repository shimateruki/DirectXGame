#include "GimmickDashPanel.h"
#include "CollisionConfig.h"
#include "Player.h"

void GimmickDashPanel::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("DashPanel");
    SetName("Gimmick_DashPanel");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetMaterialType(24);
    SetBlendMode(BlendMode::kNone);
    SetColor({ 0.25f, 0.95f, 1.0f, 1.0f });
    SetRoughness(0.62f);
    SetMetallic(0.56f);
    SetEmissive(1.0f);
    SetTextureTiling({ 1.0f, 1.0f });
    SetAutoTextureTiling(false);
    SetScale({ 2.0f, 0.25f, 1.2f });
}

void GimmickDashPanel::Update(float deltaTime) {
    BaseGimmick::Update(deltaTime);
}

bool GimmickDashPanel::OnCollision(Object3d* other) {
    Player* player = dynamic_cast<Player*>(other);
    if (!player) return true;

    CollisionInfo info = CheckCollision(other);
    if (info.isColliding && info.normal.y < -0.5f) {
        player->ApplyDashPanelBoost(0.9f, 2.15f, 0.32f);
    }

    return true;
}

std::unique_ptr<Object3d> GimmickDashPanel::Clone() const {
    auto newObj = std::make_unique<GimmickDashPanel>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

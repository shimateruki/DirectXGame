#include "GimmickDashPanel.h"
#include "CollisionConfig.h"
#include "Player.h"
#include <cmath>

void GimmickDashPanel::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("DashPanel");
    SetName("Gimmick_DashPanel");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetColor({ 1.0f, 0.55f, 0.1f, 1.0f });
    SetScale({ 2.0f, 0.25f, 1.2f });
}

void GimmickDashPanel::Update(float deltaTime) {
    pulseTimer_ += deltaTime;
    float pulse = 0.82f + std::sin(pulseTimer_ * 7.0f) * 0.08f;
    SetColor({ 1.0f, 0.45f + pulse * 0.25f, 0.08f, 1.0f });

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

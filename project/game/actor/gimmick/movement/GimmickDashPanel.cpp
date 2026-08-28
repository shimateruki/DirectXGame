#include "GimmickDashPanel.h"
#include "CollisionConfig.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

void GimmickDashPanel::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("DashPanel");
    SetName("Gimmick_DashPanel");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetMaterialType(24);
    SetBlendMode(BlendMode::kNone);
    SetColor({ 1.0f, 0.38f, 0.04f, 1.0f });
    SetRoughness(0.42f);
    SetMetallic(0.28f);
    SetEmissive(1.2f);
    SetTextureTiling({ 1.0f, 1.0f });
    SetAutoTextureTiling(false);
    SetScale({ 2.0f, 0.25f, 1.2f });
}

void GimmickDashPanel::Update(float deltaTime) {
    if (!visualBaseCaptured_) {
        baseColor_ = GetColor();
        baseEmissive_ = (std::max)(1.2f, GetEmissive());
        visualBaseCaptured_ = true;
    }

    visualTime_ += deltaTime;
    activationCooldown_ = (std::max)(0.0f, activationCooldown_ - deltaTime);
    activationPulse_ = (std::max)(0.0f, activationPulse_ - deltaTime / 0.38f);

    const float easedPulse = activationPulse_ * activationPulse_;
    const float idlePulse = 0.08f + std::sin(visualTime_ * 5.8f) * 0.045f;
    SetEmissive(baseEmissive_ + idlePulse + easedPulse * 3.4f);

    const float flashBlend = easedPulse * 0.62f;
    const Vector4 flashColor = { 1.0f, 0.86f, 0.30f, baseColor_.w };
    SetColor({
        baseColor_.x + (flashColor.x - baseColor_.x) * flashBlend,
        baseColor_.y + (flashColor.y - baseColor_.y) * flashBlend,
        baseColor_.z + (flashColor.z - baseColor_.z) * flashBlend,
        baseColor_.w,
    });

    BaseGimmick::Update(deltaTime);
}

bool GimmickDashPanel::OnCollision(Object3d* other) {
    Player* player = dynamic_cast<Player*>(other);
    if (!player) return true;

    CollisionInfo info = CheckCollision(other);
    if (info.isColliding && info.normal.y < -0.5f) {
        player->ApplyDashPanelBoost(0.9f, 2.15f, 0.32f);
        if (activationCooldown_ <= 0.0f) {
            activationPulse_ = 1.0f;
            activationCooldown_ = 0.20f;
        }
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

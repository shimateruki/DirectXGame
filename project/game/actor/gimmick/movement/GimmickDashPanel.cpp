#include "GimmickDashPanel.h"
#include "CollisionConfig.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

void GimmickDashPanel::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 地面として接触し、踏んだ時の光が読める材質を既定にする。
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("DashPanel");
    SetName("Gimmick_DashPanel");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetMaterialType(24);
    SetBlendMode(BlendMode::kNone);
    SetColor({1.0f, 0.38f, 0.04f, 1.0f});
    SetRoughness(0.42f);
    SetMetallic(0.28f);
    SetEmissive(1.2f);
    SetTextureTiling({1.0f, 1.0f});
    SetAutoTextureTiling(false);
    SetScale({2.0f, 0.25f, 1.2f});
}

void GimmickDashPanel::Update(float deltaTime) {
    // 編集済みの基準色を最初に保存し、短い加速パルスの後に戻す。
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
    const engine::Vector4 flashColor = {1.0f, 0.86f, 0.30f, baseColor_.w};
    SetColor({
        baseColor_.x + (flashColor.x - baseColor_.x) * flashBlend,
        baseColor_.y + (flashColor.y - baseColor_.y) * flashBlend,
        baseColor_.z + (flashColor.z - baseColor_.z) * flashBlend,
        baseColor_.w,
    });

    BaseGimmick::Update(deltaTime);
}

// 上面に乗ったプレイヤーへ加速を与え、発光パルスは短いクールダウンで抑えます。
bool GimmickDashPanel::OnCollision(engine::Object3d* other) {
    // 上面から踏んだプレイヤーだけを加速し、接触し続けても発光を連射しない。
    Player* player = dynamic_cast<Player*>(other);
    if (!player)
        return true;

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

std::unique_ptr<engine::Object3d> GimmickDashPanel::Clone() const {
    // 配置と調整値を複製し、実行時のパルス時計は新しい床に持ち込まない。
    auto newObj = std::make_unique<GimmickDashPanel>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

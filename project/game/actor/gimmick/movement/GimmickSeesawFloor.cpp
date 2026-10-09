#include "GimmickSeesawFloor.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>

void GimmickSeesawFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("SeesawFloor");
    SetName("Gimmick_SeesawFloor");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetScale({4.0f, 0.35f, 1.4f});
}

// 接触が途切れると目標傾きを水平へ戻し、急な角度変化を補間します。
void GimmickSeesawFloor::Update(float deltaTime) {
    if (!initializedBase_) {
        baseRotation_ = GetTransform()->rotate;
        initializedBase_ = true;
    }

    if (contactTimer_ > 0.0f) {
        contactTimer_ -= deltaTime;
        if (contactTimer_ < 0.0f)
            contactTimer_ = 0.0f;
    } else {
        targetTilt_ = 0.0f;
    }

    float t = 1.0f - std::exp(-tiltSpeed_ * deltaTime);
    Vector3 rot = GetTransform()->rotate;
    rot.x = Math::Lerp(rot.x, baseRotation_.x, t);
    rot.z = Math::Lerp(rot.z, baseRotation_.z + targetTilt_, t);
    GetTransform()->rotate = rot;
    GetTransform()->isQuaternionMaster = false;

    BaseGimmick::Update(deltaTime);
}

// プレイヤーの接触位置を床のローカル左右方向へ変換して傾きを決めます。
bool GimmickSeesawFloor::OnCollision(Object3d* other) {
    if (other && other->GetClassName() == "Player") {
        CollisionInfo info = CheckCollision(other);
        if (info.isColliding && info.normal.y < -0.5f) {
            Vector3 diff = other->GetWorldPosition() - GetWorldPosition();
            float yaw = GetTransform()->rotate.y;
            float localX = diff.x * std::cos(yaw) - diff.z * std::sin(yaw);
            float amount = std::clamp(localX / halfLength_, -1.0f, 1.0f);

            targetTilt_ = -amount * maxTilt_;
            contactTimer_ = 0.12f;
        }
    }
    return true;
}

std::unique_ptr<Object3d> GimmickSeesawFloor::Clone() const {
    auto newObj = std::make_unique<GimmickSeesawFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

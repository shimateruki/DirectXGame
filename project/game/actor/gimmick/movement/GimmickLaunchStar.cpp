#include "GimmickLaunchStar.h"

#include "CollisionConfig.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

void GimmickLaunchStar::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("LaunchStar");
    SetName("Gimmick_LaunchStar");
    SetModel(modelName);
    SetColor({ 1.0f, 0.92f, 0.28f, 1.0f });
    SetEmissive(2.2f);
    SetRoughness(0.34f);
    SetMetallic(0.12f);
    SetCollisionAttribute(CollisionAttribute::kGround);
    SetCollisionMask(0b11111111);

    ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.size = { 1.35f, 0.35f, 1.35f };
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->moveAmount = 52.0f;
    param_->jumpPower = 14.0f;
    param_->speed = 38.0f;
    baseScale_ = GetScale();
}

void GimmickLaunchStar::Update(float deltaTime) {
    if (!baseScaleCaptured_) {
        baseScale_ = GetScale();
        baseScaleCaptured_ = true;
    }
    pulseTimer_ += deltaTime;
    retriggerCooldown_ = (std::max)(0.0f, retriggerCooldown_ - deltaTime);

    const float pulse = 1.0f + std::sin(pulseTimer_ * 4.6f) * 0.055f;
    SetScale(baseScale_ * pulse);
    BaseGimmick::Update(deltaTime);
}

bool GimmickLaunchStar::OnCollision(Object3d* other) {
    if (!other || other->GetClassName() != "Player" || retriggerCooldown_ > 0.0f) {
        return true;
    }

    Player* player = dynamic_cast<Player*>(other);
    if (!player || player->IsLaunchStarActive()) {
        return true;
    }

    const CollisionInfo info = CheckCollision(other);
    if (!info.isColliding || player->GetWorldPosition().y < GetWorldPosition().y - 0.4f) {
        return true;
    }

    const Vector3 start = player->GetWorldPosition();
    const Vector3 destination = CalculateDestination(start);
    const float distance = Math::Length(destination - start);
    const float speed = param_.has_value() ? (std::max)(8.0f, param_->speed) : 38.0f;
    const float duration = std::clamp(distance / speed, 0.65f, 3.2f);
    const float arcHeight = param_.has_value() ? (std::max)(2.0f, param_->jumpPower) : 14.0f;

    player->StartLaunchStar(destination, arcHeight, duration);
    retriggerCooldown_ = duration + 0.75f;
    return true;
}

Vector3 GimmickLaunchStar::CalculateDestination(const Vector3& start) const {
    const Vector3 rotation = GetRotation();
    const float cosPitch = std::cos(rotation.x);
    Vector3 forward = {
        std::sin(rotation.y) * cosPitch,
        -std::sin(rotation.x),
        std::cos(rotation.y) * cosPitch
    };
    if (Math::Length(forward) < 0.001f) {
        forward = { 0.0f, 0.0f, 1.0f };
    }
    forward = Math::Normalize(forward);

    const float distance = param_.has_value() ? (std::max)(8.0f, param_->moveAmount) : 52.0f;
    return start + forward * distance;
}

std::unique_ptr<Object3d> GimmickLaunchStar::Clone() const {
    auto clone = std::make_unique<GimmickLaunchStar>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

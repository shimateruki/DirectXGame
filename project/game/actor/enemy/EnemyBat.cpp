#include "EnemyBat.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kHoverHeight = 3.0f;
constexpr float kSwoopDuration = 0.75f;
constexpr float kSwoopRange = 10.0f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}
}

void EnemyBat::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_Bat");
    SetEnemyType("Bat");
    SetColor({ 0.35f, 0.18f, 0.74f, 1.0f });
    defaultColor_ = GetColor();
    SetScale({ 1.4f, 0.55f, 0.85f });

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(0.75f);
}

void EnemyBat::Update(float deltaTime) {
    if (isCarried_) {
        return;
    }

    CaptureHomePosition();
    hoverTimer_ += deltaTime;
    swoopCooldown_ = (std::max)(0.0f, swoopCooldown_ - deltaTime);
    if (swoopTimer_ > 0.0f) {
        swoopTimer_ = (std::max)(0.0f, swoopTimer_ - deltaTime);
    }

    Vector3 desired = homePosition_;
    Vector3 direction = { 0.0f, 0.0f, 1.0f };
    float distance = 9999.0f;

    if (target_) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        direction = NormalizePlanar(toTarget);
        distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
        if (distance <= detectionRange_) {
            if (distance <= kSwoopRange && swoopCooldown_ <= 0.0f && swoopTimer_ <= 0.0f) {
                swoopTimer_ = kSwoopDuration;
                swoopCooldown_ = 2.4f;
            }
            desired = CalcDesiredPosition(distance, direction);
            UpdateFacing(direction);
        } else {
            desired.y += std::sin(hoverTimer_ * 2.6f) * 0.55f;
        }
    }

    Vector3 toDesired = desired - GetTranslate();
    const float maxSpeed = param_.has_value() ? (std::max)(1.0f, param_->speed) : 7.0f;
    const float speed = swoopTimer_ > 0.0f ? maxSpeed * 1.8f : maxSpeed;
    Vector3 velocity = toDesired * std::min(1.0f, deltaTime * speed);
    if (deltaTime > 0.001f) {
        velocity = velocity / deltaTime;
    }
    SetVelocity(velocity);

    const float wing = std::sin(hoverTimer_ * 14.0f) * 0.22f;
    Vector3 scale = GetScale();
    scale.y = baseScale_.y + std::abs(wing) * baseScale_.y * 0.45f;
    scale.x = baseScale_.x + wing * baseScale_.x;
    scale.z = baseScale_.z;
    SetScale(scale);

    BaseEnemy::Update(deltaTime);
}

std::unique_ptr<Object3d> EnemyBat::Clone() const {
    auto clone = std::make_unique<EnemyBat>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyBat::CaptureHomePosition() {
    if (hasHomePosition_) return;
    homePosition_ = GetTranslate();
    baseScale_ = GetScale();
    if (homePosition_.y < kHoverHeight) {
        homePosition_.y += kHoverHeight;
        SetTranslate(homePosition_);
    }
    hasHomePosition_ = true;
}

void EnemyBat::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f) return;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, std::atan2(direction.x, direction.z), 0.18f));
}

Vector3 EnemyBat::CalcDesiredPosition(float distance, const Vector3& direction) const {
    Vector3 targetPos = target_->GetTranslate();
    targetPos.y += swoopTimer_ > 0.0f ? 0.6f : kHoverHeight + std::sin(hoverTimer_ * 3.2f) * 0.55f;

    if (swoopTimer_ > 0.0f) {
        return targetPos;
    }

    Vector3 side = { direction.z, 0.0f, -direction.x };
    const float orbit = std::sin(hoverTimer_ * 1.4f) * 3.0f;
    const float keepDistance = std::clamp(distance, 3.5f, 6.0f);
    return targetPos - direction * keepDistance + side * orbit;
}

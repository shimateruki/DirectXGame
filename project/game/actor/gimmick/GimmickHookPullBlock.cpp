#define NOMINMAX
#include "GimmickHookPullBlock.h"
#include "CollisionConfig.h"
#include "DebugConsole.h"
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cassert>

void GimmickHookPullBlock::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("HookPullBlock");
    SetName("Gimmick_HookPullBlock");
    SetCollisionAttribute(kGround);
    SetCollisionMask(0b11111111);
    SetStatic(false);
    SetColor({ 0.55f, 0.85f, 1.0f, 1.0f });

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->speed = 42.0f;
    param_->gravity = 50.0f;
    param_->maxFallSpeed = 60.0f;
}

void GimmickHookPullBlock::Update(float deltaTime) {
    if (!param_.has_value()) {
        param_.emplace();
    }

    pullTimer_ = std::max(0.0f, pullTimer_ - deltaTime);

    if (pullTimer_ > 0.0f) {
        Vector3 toTarget = pullTarget_ - GetWorldPosition();
        toTarget.y = 0.0f;

        float dist = Math::Length(toTarget);
        if (dist > 0.05f) {
            Vector3 dir = Math::Normalize(toTarget);
            float pullSpeed = std::max(1.0f, param_->speed);
            velocity_.x = Math::Lerp(velocity_.x, dir.x * pullSpeed, 0.18f);
            velocity_.z = Math::Lerp(velocity_.z, dir.z * pullSpeed, 0.18f);
        }
    } else {
        velocity_.x *= 0.86f;
        velocity_.z *= 0.86f;
        if (std::abs(velocity_.x) < 0.02f) velocity_.x = 0.0f;
        if (std::abs(velocity_.z) < 0.02f) velocity_.z = 0.0f;
    }

    velocity_.y -= param_->gravity * deltaTime;
    if (velocity_.y < -param_->maxFallSpeed) {
        velocity_.y = -param_->maxFallSpeed;
    }

    GetTransform()->translate += velocity_ * deltaTime;

    float speedXZ = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    if (speedXZ > 0.05f) {
        Vector3 rot = GetRotation();
        rot.x += velocity_.z * deltaTime * 0.35f;
        rot.z -= velocity_.x * deltaTime * 0.35f;
        SetRotation(rot);
    }

    BaseGimmick::Update(deltaTime);
}

bool GimmickHookPullBlock::OnCollision(Object3d* other) {
    if (!other || other == this) return false;
    if (!(other->GetCollisionAttribute() & kAllSolid)) return true;
    if (other->GetGimmickType() == "HookPullBlock") return true;

    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) return false;

    GetTransform()->translate += info.normal * info.penetration;

    float dot = Math::Dot(velocity_, info.normal);
    if (dot < 0.0f) {
        velocity_ = velocity_ - info.normal * dot;
    }

    if (info.normal.y > 0.65f && velocity_.y < 0.0f) {
        velocity_.y = 0.0f;
    }

    return true;
}

void GimmickHookPullBlock::StartHookPull(const Vector3& hookOwnerPos) {
    Vector3 toOwner = hookOwnerPos - GetWorldPosition();
    toOwner.y = 0.0f;
    if (Math::Length(toOwner) < 0.05f) return;

    Vector3 dir = Math::Normalize(toOwner);
    pullTarget_ = hookOwnerPos - dir * 4.0f;
    pullTarget_.y = GetWorldPosition().y;
    pullTimer_ = 0.45f;

    float pullSpeed = param_.has_value() ? std::max(1.0f, param_->speed) : 42.0f;
    velocity_.x = dir.x * pullSpeed;
    velocity_.z = dir.z * pullSpeed;
    if (velocity_.y < 3.0f) velocity_.y = 3.0f;

    DebugConsole::GetInstance()->AddLog("HookPullBlock pulled");
}

std::unique_ptr<Object3d> GimmickHookPullBlock::Clone() const {
    auto newObj = std::make_unique<GimmickHookPullBlock>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

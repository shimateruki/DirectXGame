#include "EnemyBat.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>

namespace {
// コウモリの旋回、予兆、急降下に関する調整値
constexpr float kHoverHeight = 5.2f;
constexpr float kOrbitRadius = 9.0f;
constexpr float kOrbitBobHeight = 0.55f;
constexpr float kOrbitAngularSpeed = 0.85f;
constexpr float kTelegraphDuration = 0.55f;
constexpr float kDiveDuration = 0.95f;
constexpr float kRecoverDuration = 1.0f;
constexpr float kDiveCooldown = 3.8f;
constexpr float kDiveStartRange = 15.0f;
constexpr float kDiveHitHeight = 0.75f;
constexpr float kOrbitSpeed = 2.4f;
constexpr float kTelegraphSpeed = 2.0f;
constexpr float kDiveSpeed = 7.2f;
constexpr float kRecoverSpeed = 3.0f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}
}

// コウモリ敵の初期化
void EnemyBat::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_Bat");
    SetEnemyType("Bat");
    SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    defaultColor_ = GetColor();
    SetScale({ 0.6f, 0.6f, 0.6f });
    animName_ = "ArmatureAction";
    isAnimLoop_ = true;
    animationTime_ = 0.0f;
    EnsureAnimation();

    SetCollisionAttribute(kEnemy);
    SetPlayerContactEnabled(false);
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(0.85f);
}

// 旋回から急降下までのステート制御
void EnemyBat::Update(float deltaTime) {
    if (UpdateInactiveState(deltaTime)) {
        return;
    }

    CaptureHomePosition();
    UpdateTimers(deltaTime);

    Vector3 desired = homePosition_;
    float moveSpeed = kOrbitSpeed;
    if (target_) {
        UpdateTargetBehavior(deltaTime, desired, moveSpeed);
    }
    else {
        UpdateWanderBehavior(deltaTime, desired, moveSpeed);
    }

    ApplyMovement(deltaTime, desired, moveSpeed);
    BaseEnemy::Update(deltaTime);
}

bool EnemyBat::UpdateInactiveState(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        BaseEnemy::Update(deltaTime);
        return true;
    }
    if (isCarried_) {
        return true;
    }

    EnsureAnimation();
    if (IsThrowRecovering()) {
        BaseEnemy::Update(deltaTime);
        return true;
    }
    return false;
}

void EnemyBat::UpdateTimers(float deltaTime) {
    hoverTimer_ += deltaTime;
    diveCooldown_ = (std::max)(0.0f, diveCooldown_ - deltaTime);
    stateTimer_ = (std::max)(0.0f, stateTimer_ - deltaTime);
    if (state_ != BatState::Dive) {
        SetPlayerContactEnabled(false);
    }
}

void EnemyBat::UpdateTargetBehavior(float deltaTime, Vector3& desired, float& moveSpeed) {
    Vector3 direction = { 0.0f, 0.0f, 1.0f };
    Vector3 toTarget = target_->GetTranslate() - GetTranslate();
    direction = NormalizePlanar(toTarget);
    const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

    if (UpdateNoticeReaction(deltaTime, distance, detectionRange_, direction)) {
        desired = GetTranslate();
        desired.y += 0.25f;
        moveSpeed = 0.0f;
        UpdateFacing(direction);
        return;
    }

    if (distance > detectionRange_) {
        state_ = BatState::Orbit;
        UpdateWanderBehavior(deltaTime, desired, moveSpeed);
        return;
    }

    if (state_ == BatState::Orbit && diveCooldown_ <= 0.0f && distance <= kDiveStartRange) {
        state_ = BatState::Telegraph;
        stateTimer_ = kTelegraphDuration;
    }

    switch (state_) {
    case BatState::Orbit:
        desired = CalcOrbitPosition();
        moveSpeed = kOrbitSpeed;
        break;
    case BatState::Telegraph:
        desired = CalcOrbitPosition();
        desired.y += 1.0f;
        moveSpeed = kTelegraphSpeed;
        if (stateTimer_ <= 0.0f) {
            diveTarget_ = target_->GetTranslate();
            diveTarget_.y += kDiveHitHeight;
            state_ = BatState::Dive;
            stateTimer_ = kDiveDuration;
            SetPlayerContactEnabled(true);
        }
        break;
    case BatState::Dive:
        desired = diveTarget_;
        moveSpeed = kDiveSpeed;
        if (stateTimer_ <= 0.0f || Math::Length(GetTranslate() - diveTarget_) < 1.2f) {
            state_ = BatState::Recover;
            stateTimer_ = kRecoverDuration;
            diveCooldown_ = kDiveCooldown;
            SetPlayerContactEnabled(false);
        }
        break;
    case BatState::Recover:
        desired = CalcOrbitPosition();
        moveSpeed = kRecoverSpeed;
        if (stateTimer_ <= 0.0f) {
            state_ = BatState::Orbit;
        }
        break;
    }
    UpdateFacing(direction);
}

void EnemyBat::UpdateWanderBehavior(float deltaTime, Vector3& desired, float& moveSpeed) {
    desired = GetWanderTargetPosition(deltaTime, 0.55f);
    desired.y += std::sin(hoverTimer_ * 2.6f) * 0.55f;
    moveSpeed = kOrbitSpeed * 0.75f;
    SetPlayerContactEnabled(false);
    UpdateFacing(NormalizePlanar(desired - GetTranslate()));
}

void EnemyBat::ApplyMovement(float deltaTime, const Vector3& desired, float moveSpeed) {
    Vector3 toDesired = desired - GetTranslate();
    const float paramSpeed = param_.has_value() ? (std::max)(0.5f, param_->speed) : kOrbitSpeed;
    const float speed = std::min(moveSpeed, paramSpeed * (moveSpeed / kOrbitSpeed));
    Vector3 velocity = toDesired * std::min(1.0f, deltaTime * speed);
    if (deltaTime > 0.001f) {
        velocity = velocity / deltaTime;
    }
    SetVelocity(velocity);
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

// 急降下AIの補助処理
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

void EnemyBat::EnsureAnimation() {
    Model* model = GetModel();
    if (!model) {
        return;
    }

    const auto& animations = model->GetModelData().animations;
    if (animations.empty()) {
        return;
    }

    if (animName_.empty() || !model->GetAnimation(animName_)) {
        animName_ = animations.front().name;
        animationTime_ = 0.0f;
    }
    isAnimLoop_ = true;
}

void EnemyBat::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f) return;
    SetRotationY(Math::LerpShortAngle(GetRotation().y, std::atan2(direction.x, direction.z), 0.18f));
}

Vector3 EnemyBat::CalcOrbitPosition() const {
    Vector3 targetPos = target_->GetTranslate();
    const float angle = hoverTimer_ * kOrbitAngularSpeed;
    targetPos.x += std::cos(angle) * kOrbitRadius;
    targetPos.y += kHoverHeight + std::sin(hoverTimer_ * 2.2f) * kOrbitBobHeight;
    targetPos.z += std::sin(angle) * kOrbitRadius;
    return targetPos;
}

void EnemyBat::SetPlayerContactEnabled(bool enabled) {
    uint32_t mask = kPlayerAttack | kAttributePlayerBullet;
    if (enabled) {
        mask |= kPlayer;
    }
    SetCollisionMask(mask);
}

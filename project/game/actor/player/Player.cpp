#define NOMINMAX
#include "Player.h"

#include "Camera.h"
#include "CameraManager.h"
#include "CollisionConfig.h"
#include "Object3dCommon.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kTwoPi = kPi * 2.0f;

float NormalizeAngle(float angle) {
    while (angle > kPi) {
        angle -= kTwoPi;
    }
    while (angle < -kPi) {
        angle += kTwoPi;
    }
    return angle;
}

Vector3 NormalizePlanar(const Vector3& value) {
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.0001f) {
        return {};
    }
    return { value.x / length, 0.0f, value.z / length };
}
}

Player::~Player() {
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera && camera->GetFollowTarget() == this) {
        camera->SetFollowTarget(nullptr);
    }
}

void Player::Initialize(
    Object3dCommon* common,
    InputManager* inputManager,
    ParticleSystem* particleSystem,
    SpriteCommon* spriteCommon) {
    (void)particleSystem;
    (void)spriteCommon;

    Character::Initialize(common);
    inputManager_ = inputManager;
    SetName("Player");
    SetClassName("Player");
    SetSaveCategory("Player");
    SetModel("Primitives/cube");
    SetScale({ 0.8f, 1.8f, 0.8f });
    SetCollisionAttribute(kPlayer);
    SetCollisionMask(kAllSolid);

    Object3d::ColliderConfig collider = GetColliderConfig();
    collider.type = ColliderType::kCylinder;
    collider.center = { 0.0f, 0.9f, 0.0f };
    collider.size = { 0.4f, 0.9f, 0.4f };
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->gravity = 24.0f;
    param_->maxFallSpeed = 40.0f;
    param_->motorContinuousCollision = true;
    param_->motorSnapToGround = true;
    param_->motorStepHeight = 0.35f;
    param_->motorGroundProbeDistance = 0.22f;
    param_->motorSkinWidth = 0.03f;

    respawnPosition_ = GetTranslate();

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        // Player破棄時にはデストラクタで解除し、Cameraへ古いポインタを残しません。
        camera->SetFollowTarget(this);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
    }
}

void Player::Update(float deltaTime) {
    Vector3 velocity = GetVelocity();
    if (isControlActive_ && inputManager_) {
        const Vector3 direction = CalculateMoveDirection();
        velocity.x = direction.x * moveSpeed_;
        velocity.z = direction.z * moveSpeed_;
        UpdateFacing(direction, deltaTime);

        if (inputManager_->IsKeyTriggered(DIK_SPACE) && IsGrounded()) {
            velocity.y = jumpPower_;
            SetGrounded(false);
        }
    } else {
        velocity.x = 0.0f;
        velocity.z = 0.0f;
    }

    SetVelocity(velocity);
    Character::Update(deltaTime);
}

Vector3 Player::CalculateMoveDirection() const {
    Vector3 input{};
    if (inputManager_->IsKeyPressed(DIK_W)) {
        input.z += 1.0f;
    }
    if (inputManager_->IsKeyPressed(DIK_S)) {
        input.z -= 1.0f;
    }
    if (inputManager_->IsKeyPressed(DIK_D)) {
        input.x += 1.0f;
    }
    if (inputManager_->IsKeyPressed(DIK_A)) {
        input.x -= 1.0f;
    }

    input = NormalizePlanar(input);
    if (input.x == 0.0f && input.z == 0.0f) {
        return {};
    }

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) {
        return input;
    }

    // 入力をアクティブカメラのYawへ合わせ、エディター視点でも操作方向を直感的に保ちます。
    const Vector3 rotation = camera->GetRotation();
    const Vector3 forward = { std::sin(rotation.y), 0.0f, std::cos(rotation.y) };
    const Vector3 right = { forward.z, 0.0f, -forward.x };
    return NormalizePlanar(forward * input.z + right * input.x);
}

void Player::UpdateFacing(const Vector3& direction, float deltaTime) {
    if (direction.x == 0.0f && direction.z == 0.0f) {
        return;
    }

    const float targetYaw = std::atan2(direction.x, direction.z);
    const float currentYaw = GetRotation().y;
    // -PIからPIへ正規化し、反対向き付近でも最短方向へ旋回します。
    const float deltaYaw = NormalizeAngle(targetYaw - currentYaw);
    const float blend = 1.0f - std::exp(-turnResponse_ * (std::max)(deltaTime, 0.0f));
    SetRotationY(currentYaw + deltaYaw * blend);
}

std::unique_ptr<Object3d> Player::Clone() const {
    auto clone = std::make_unique<Player>();
    clone->Initialize(common_, inputManager_);
    clone->ImportFromJson(const_cast<Player*>(this)->ExportToJson());
    clone->moveSpeed_ = moveSpeed_;
    clone->jumpPower_ = jumpPower_;
    clone->turnResponse_ = turnResponse_;
    clone->respawnPosition_ = respawnPosition_;
    return clone;
}

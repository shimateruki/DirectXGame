#define NOMINMAX
#include "GimmickFireCannon.h"
#include "BaseScene.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "Player.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDefaultProjectileSpeed = 13.0f;
constexpr float kDefaultFireInterval = 1.35f;
constexpr float kDefaultProjectileRadius = 0.55f;
constexpr float kDefaultDetectionRange = 45.0f;
constexpr float kDefaultTurnSpeedDegrees = 360.0f;
}

void GimmickFireCannon::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("FireCannon");
    SetName("Gimmick_FireCannon");
    SetModel(modelName);
    SetColor({ 0.22f, 0.11f, 0.08f, 1.0f });
    SetRoughness(0.65f);
    SetMetallic(0.05f);
    SetEmissive(1.1f);
    SetScale({ 0.55f, 0.55f, 1.1f });
    SetCollisionAttribute(CollisionAttribute::kGround);
    SetCollisionMask(0b11111111);
    SetStatic(false);

    Object3d::ColliderConfig colConfig;
    colConfig.type = ColliderType::kOBB;
    colConfig.size = { 1.0f, 1.0f, 1.0f };
    SetColliderConfig(colConfig);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->speed = kDefaultProjectileSpeed;
    param_->interval = kDefaultFireInterval;
    param_->moveAmount = kDefaultProjectileRadius;
    param_->moveSpeed = kDefaultTurnSpeedDegrees;
    param_->detectionRange = kDefaultDetectionRange;
    param_->actionMode = 1;
    param_->startActive = true;
    param_->returnOnOff = true;
}

void GimmickFireCannon::Update(float deltaTime) {
    if (!param_.has_value()) {
        param_.emplace();
    }

    if (!IsPlaying()) {
        if (initializedForPlay_) {
            SetTranslate(basePosition_);
            SetRotation(baseRotation_);
            SetScale(baseScale_);
        }
        initializedForPlay_ = false;
        active_ = param_->startActive;
        fireTimer_ = 0.0f;
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        basePosition_ = GetTranslate();
        baseRotation_ = GetRotation();
        baseScale_ = GetScale();
        active_ = param_->startActive;
        fireTimer_ = GetFireInterval() * 0.35f;
        initializedForPlay_ = true;
    }

    Vector3 fireDirection = {};
    const bool canFire = active_ && FindFireDirection(fireDirection);
    if (canFire && param_->actionMode == 1) {
        RotateToward(fireDirection, deltaTime);
        fireDirection = GetForwardDirection();
    }

    if (canFire) {
        fireTimer_ -= deltaTime;
        if (fireTimer_ <= 0.0f) {
            FireProjectile(fireDirection);
            fireTimer_ = GetFireInterval();
        }
    }
    else {
        fireTimer_ = (std::min)(fireTimer_, GetFireInterval() * 0.35f);
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickFireCannon::OnTrigger() {
    OnSwitchEvent(!active_);
}

void GimmickFireCannon::OnSwitchEvent(bool active) {
    if (!param_.has_value()) {
        param_.emplace();
    }
    if (!active && !param_->returnOnOff) {
        return;
    }
    active_ = active;
}

bool GimmickFireCannon::IsPlaying() const {
    SceneManager* sceneManager = SceneManager::GetInstance();
    return sceneManager && sceneManager->IsPlaying();
}

bool GimmickFireCannon::FindFireDirection(Vector3& outDirection) const {
    if (!param_.has_value() || param_->actionMode == 0) {
        outDirection = GetForwardDirection();
        return true;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager) {
        return false;
    }

    BaseScene* scene = sceneManager->GetCurrentScene();
    if (!scene) {
        return false;
    }

    Player* player = scene->GetPlayer();
    if (!player) {
        return false;
    }

    Vector3 target = player->GetWorldPosition();
    target.y += 0.65f;

    const Vector3 muzzle = GetMuzzlePosition(GetForwardDirection());
    Vector3 diff = target - muzzle;
    const float distance = Math::Length(diff);
    const float range = (param_->detectionRange > 0.0f) ? param_->detectionRange : kDefaultDetectionRange;
    if (distance > range || distance < 0.001f) {
        return false;
    }

    outDirection = Math::Normalize(diff);
    return true;
}

Vector3 GimmickFireCannon::GetForwardDirection() const {
    const Vector3 rot = GetRotation();
    const float cosPitch = std::cos(rot.x);
    Vector3 forward = {
        std::sin(rot.y) * cosPitch,
        -std::sin(rot.x),
        std::cos(rot.y) * cosPitch
    };

    if (Math::Length(forward) < 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return Math::Normalize(forward);
}

Vector3 GimmickFireCannon::GetMuzzlePosition(const Vector3& direction) const {
    const Vector3 scale = GetScale();
    Vector3 muzzle = GetWorldPosition();
    muzzle += direction * ((std::max)(scale.z, 0.5f) * 0.95f);
    muzzle.y += (std::max)(scale.y, 0.5f) * 0.18f;
    return muzzle;
}

void GimmickFireCannon::RotateToward(const Vector3& direction, float deltaTime) {
    const float horizontalLength = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    if (horizontalLength < 0.001f) {
        return;
    }

    const float targetYaw = std::atan2(direction.x, direction.z);
    const float targetPitch = std::atan2(-direction.y, horizontalLength);
    const float t = std::clamp(GetTurnSpeedRadians() * deltaTime, 0.0f, 1.0f);

    Vector3 rot = GetRotation();
    rot.y = Math::LerpShortAngle(rot.y, targetYaw, t);
    rot.x = Math::LerpShortAngle(rot.x, targetPitch, t);
    SetRotation(rot);
}

void GimmickFireCannon::FireProjectile(const Vector3& direction) {
    const float radius = GetProjectileRadius();
    const float speed = GetProjectileSpeed();
    const float range = param_.has_value() ? (std::max)(1.0f, param_->detectionRange) : kDefaultDetectionRange;
    const float life = std::clamp(range / (std::max)(1.0f, speed), 1.0f, 8.0f);

    BulletVisualConfig visual;
    visual.materialType = 11;
    visual.blendMode = BlendMode::kAdd;
    visual.color = { 1.0f, 0.42f, 0.10f, 0.95f };
    visual.emissive = 5.0f;
    visual.visualScale = radius * 1.75f;
    visual.effectType = 1.0f;
    visual.effectScale = 1.15f;
    visual.effectSoftness = 0.56f;
    visual.effectIntensity = 1.35f;
    visual.billboardScale = 1.0f;
    visual.texturePath = "Resources/sprite/common/white.png";

    BulletManager::GetInstance()->Fire(
        GetMuzzlePosition(direction),
        direction * speed,
        CollisionAttribute::kEnemyAttack,
        CollisionAttribute::kPlayer | kAllSolid,
        "Primitives/sphere",
        radius,
        life,
        visual,
        1.0f,
        {},
        DamageType::Fire);
}

float GimmickFireCannon::GetProjectileSpeed() const {
    return param_.has_value() ? (std::max)(1.0f, param_->speed) : kDefaultProjectileSpeed;
}

float GimmickFireCannon::GetFireInterval() const {
    return param_.has_value() ? (std::max)(0.08f, param_->interval) : kDefaultFireInterval;
}

float GimmickFireCannon::GetProjectileRadius() const {
    return param_.has_value() ? std::clamp(param_->moveAmount, 0.1f, 5.0f) : kDefaultProjectileRadius;
}

float GimmickFireCannon::GetTurnSpeedRadians() const {
    const float degrees = param_.has_value() ? (std::max)(1.0f, param_->moveSpeed) : kDefaultTurnSpeedDegrees;
    return degrees * (kPi / 180.0f);
}

std::unique_ptr<Object3d> GimmickFireCannon::Clone() const {
    auto newObj = std::make_unique<GimmickFireCannon>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

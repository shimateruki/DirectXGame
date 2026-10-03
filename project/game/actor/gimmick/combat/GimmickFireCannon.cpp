#define NOMINMAX
#include "GimmickFireCannon.h"
#include "BaseScene.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
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

    landingWarning_.Initialize(common);
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
        landingWarning_.Hide();
        targetLocked_ = false;
        flightTimer_ = recoilTimer_ = 0.0f;
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

    if (param_->actionMode == 2) {
        UpdateMortar(deltaTime);
        BaseGimmick::Update(deltaTime);
        return;
    }
    landingWarning_.Hide();
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
    if (!active) { targetLocked_ = false; landingWarning_.Hide(); }
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

void GimmickFireCannon::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    BaseGimmick::Draw(pointLightResource, spotLightResource);
    landingWarning_.DrawGround(pointLightResource, spotLightResource);
}

void GimmickFireCannon::UpdateMortar(float deltaTime) {
    // 台座は水平に保ち、砲弾の弾道だけを重力で計算する。
    const float dt = (std::max)(0.0f, deltaTime);
    const float blast = std::clamp(param_->healAmount, 1.0f, 8.0f);
    const float warning = std::clamp(param_->shakeDuration, 0.35f, 4.0f);
    const float gravity = std::clamp(param_->gravity, 4.0f, 60.0f);
    const float radius = GetProjectileRadius();
    recoilTimer_ = (std::max)(0.0f, recoilTimer_ - dt);
    const float squash = std::sin(recoilTimer_ / 0.28f * kPi) * 0.06f;
    SetScale({baseScale_.x * (1.0f + squash), baseScale_.y * (1.0f - squash), baseScale_.z});
    landingWarning_.Update(dt);
    if (flightTimer_ > 0.0f) {
        flightTimer_ -= dt;
        if (flightTimer_ <= 0.0f) landingWarning_.Hide();
        return;
    }
    if (!active_) { landingWarning_.Hide(); targetLocked_ = false; return; }
    if (!targetLocked_) {
        fireTimer_ -= dt;
        if (fireTimer_ > 0.0f) return;
        auto* scene = SceneManager::GetInstance()->GetCurrentScene();
        auto* player = scene ? scene->GetPlayer() : nullptr;
        if (!player) return;
        const Vector3 target = player->GetWorldPosition();
        if (Math::Length(target - GetWorldPosition()) > param_->detectionRange) return;
        PhysicsQueryFilter filter;
        filter.mask = kAllSolid;
        filter.ignoredObject = this;
        const auto ground = CollisionManager::GetInstance()->Raycast(target + Vector3{0,1,0}, {0,-1,0}, 18.0f, filter);
        if (!ground.isHit) return;
        lockedTarget_ = ground.hitPoint;
        Vector3 direction = lockedTarget_ - GetWorldPosition();
        if (direction.x * direction.x + direction.z * direction.z < 100.0f) return;
        // 予告開始時に狙いを固定し、回避後のプレイヤーを追いかけない。
        SetRotation({0.0f, std::atan2(direction.x, direction.z), 0.0f});
        targetLocked_ = true;
        warningTimer_ = warning;
    }
    warningTimer_ -= dt;
    landingWarning_.ShowCircle(lockedTarget_ + Vector3{0,0.08f,0}, blast,
        std::clamp(1.0f - warningTimer_ / warning, 0.0f, 1.0f), {1.0f,0.34f,0.05f,0.65f});
    if (warningTimer_ > 0.0f) return;
    const Vector3 forward = GetForwardDirection();
    const Vector3 muzzle = GetWorldPosition() + forward * (2.55f * baseScale_.z)
        + Vector3{0,2.73f * baseScale_.y,0};
    const Vector3 target = lockedTarget_ + Vector3{0,radius + 0.03f,0};
    const float apex = (std::max)(muzzle.y, target.y) + std::clamp(param_->jumpPower, 3.0f, 20.0f);
    const float vy = std::sqrt(2.0f * gravity * (apex - muzzle.y));
    const float flight = vy / gravity + std::sqrt(2.0f * (apex - target.y) / gravity);
    const Vector3 velocity = {(target.x-muzzle.x)/flight, vy, (target.z-muzzle.z)/flight};
    BulletVisualConfig visual;
    visual.visualScale = radius;
    visual.explosionRadius = blast;
    visual.color = {1,1,1,1};
    visual.emissive = 1.0f;
    BulletManager::GetInstance()->Fire(muzzle, velocity, CollisionAttribute::kEnemyAttack,
        CollisionAttribute::kPlayer | kAllSolid, "Gimmicks/volcanic_shell", radius,
        flight + 2.0f, visual, 1.0f, {}, DamageType::Explosion, gravity);
    if (auto* effects = MeshEffectManager::GetInstance())
        effects->SpawnEffectAt("Resources/json/effect/effect_bomb_core_flash.json", muzzle, {0,0,0}, {.4f,.4f,.4f});
    if (auto* particles = GPUParticleManager::GetInstance(); particles && particles->IsInitialized())
        particles->EmitDirected("hit_bomb_fire_sparks", muzzle, forward, 0.6f);
    targetLocked_ = false;
    flightTimer_ = flight;
    fireTimer_ = GetFireInterval();
    recoilTimer_ = 0.28f;
}

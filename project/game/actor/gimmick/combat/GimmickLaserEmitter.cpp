#define NOMINMAX
#include "GimmickLaserEmitter.h"
#include "BaseScene.h"
#include "CollisionConfig.h"
#include "EventManager.h"
#include "GimmickLaserNode.h"
#include "Player.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>

void GimmickLaserEmitter::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("LaserEmitter");
    SetName("Gimmick_LaserEmitter");
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
    SetColor({ 1.0f, 0.08f, 0.05f, 0.9f });
    SetBlendMode(BlendMode::kAdd);
    SetMaterialType(12);
    SetTexture("Resources/sprite/common/white.png");
    SetEmissive(6.0f);
    SetScale({ 0.25f, 0.25f, 1.0f });
    SetStatic(false);

    Object3d::ColliderConfig colConfig;
    colConfig.type = ColliderType::kOBB;
    colConfig.size = { 1.0f, 1.0f, 1.0f };
    SetColliderConfig(colConfig);

    if (!param_.has_value()) param_.emplace();
    param_->speed = 10.0f;
    param_->interval = 0.7f;
    param_->moveAmount = 0.14f;
    param_->startActive = true;
    param_->returnOnOff = true;
}

void GimmickLaserEmitter::Update(float deltaTime) {
    if (!param_.has_value()) param_.emplace();

    bool isPlaying = false;
    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    if (!isPlaying) {
        if (initializedForPlay_) {
            SetTranslate(basePosition_);
            SetRotation(baseRotation_);
            SetScale(baseScale_);
        }
        initializedForPlay_ = false;
        active_ = param_->startActive;
        ApplyCollisionState(true);
        SetIsVisible(true);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        basePosition_ = GetTransform()->translate;
        baseRotation_ = GetTransform()->rotate;
        baseScale_ = GetTransform()->scale;
        active_ = param_->startActive;
        damageCooldownTimer_ = 0.0f;
        pulseTimer_ = 0.0f;
        initializedForPlay_ = true;
    }

    if (damageCooldownTimer_ > 0.0f) {
        damageCooldownTimer_ = (std::max)(0.0f, damageCooldownTimer_ - deltaTime);
    }

    Vector3 target = {};
    const bool hasTarget = FindTargetPosition(target);
    if (!active_ || !hasTarget) {
        SetTranslate(basePosition_);
        SetRotation(baseRotation_);
        SetScale(baseScale_);
        SetColor({ 0.75f, 0.05f, 0.04f, hasTarget ? 0.35f : 0.75f });
        SetIsVisible(true);
        ApplyCollisionState(false);
        BaseGimmick::Update(deltaTime);
        return;
    }

    pulseTimer_ += deltaTime;
    ApplyBeamTransform(basePosition_, target);
    ApplyCollisionState(true);

    const float pulse = 0.78f + std::sin(pulseTimer_ * 12.0f) * 0.14f;
    SetColor({ 1.0f, 0.04f + pulse * 0.08f, 0.02f, 0.84f + pulse * 0.12f });
    SetIsVisible(true);

    BaseGimmick::Update(deltaTime);
}

bool GimmickLaserEmitter::OnCollision(Object3d* other) {
    if (!active_ || damageCooldownTimer_ > 0.0f) return true;

    Player* player = dynamic_cast<Player*>(other);
    if (!player) return true;

    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) return true;

    Vector3 knockbackDir = player->GetWorldPosition() - basePosition_;
    knockbackDir.y = 0.0f;
    if (Math::Length(knockbackDir) < 0.001f) {
        knockbackDir = { 0.0f, 0.0f, 1.0f };
    }
    knockbackDir = Math::Normalize(knockbackDir);

    DamageEvent damageEvent;
    damageEvent.target = player;
    damageEvent.attacker = this;
    damageEvent.damageAmount = GetDamage();
    damageEvent.knockbackVelocity = { knockbackDir.x * 12.0f, 8.0f, knockbackDir.z * 12.0f };
    EventManager::GetInstance()->Dispatch(damageEvent);

    damageCooldownTimer_ = GetDamageInterval();
    return true;
}

void GimmickLaserEmitter::OnTrigger() {
    OnSwitchEvent(!active_);
}

void GimmickLaserEmitter::OnSwitchEvent(bool active) {
    if (!param_.has_value()) param_.emplace();
    if (!active && !param_->returnOnOff) return;
    active_ = active;
}

bool GimmickLaserEmitter::FindTargetPosition(Vector3& outTarget) const {
    if (GetTargetID() == -1) return false;

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager) return false;

    BaseScene* scene = sceneManager->GetCurrentScene();
    if (!scene) return false;

    Object3d* target = scene->FindObjectByEventID(GetTargetID());
    if (!target || target == this) return false;

    if (GimmickLaserNode* targetNode = dynamic_cast<GimmickLaserNode*>(target)) {
        outTarget = targetNode->GetLaserAnchorPosition();
    }
    else {
        outTarget = target->GetWorldPosition();
    }
    return true;
}

void GimmickLaserEmitter::ApplyBeamTransform(const Vector3& source, const Vector3& target) {
    Vector3 diff = target - source;
    const float length = Math::Length(diff);
    if (length < 0.001f) {
        ApplyCollisionState(false);
        return;
    }

    const float pulse = 0.94f + std::sin(pulseTimer_ * 18.0f) * 0.06f;
    const float thickness = GetThickness() * pulse;
    const Vector3 midpoint = source + diff * 0.5f;
    const float yaw = std::atan2(diff.x, diff.z);
    const float horizontalLength = std::sqrt(diff.x * diff.x + diff.z * diff.z);
    const float pitch = std::atan2(-diff.y, horizontalLength);

    SetTranslate(midpoint);
    SetRotation({ pitch, yaw, 0.0f });
    SetScale({ thickness, thickness, length * 0.5f });
}

void GimmickLaserEmitter::ApplyCollisionState(bool enabled) {
    if (enabled) {
        SetCollisionAttribute(CollisionAttribute::kTrigger);
        SetCollisionMask(CollisionAttribute::kPlayer);
    }
    else {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
    }
}

float GimmickLaserEmitter::GetDamage() const {
    return param_.has_value() ? (std::max)(0.0f, param_->speed) : 10.0f;
}

float GimmickLaserEmitter::GetDamageInterval() const {
    return param_.has_value() ? (std::max)(0.05f, param_->interval) : 0.7f;
}

float GimmickLaserEmitter::GetThickness() const {
    return param_.has_value() ? (std::max)(0.03f, param_->moveAmount) : 0.25f;
}

std::unique_ptr<Object3d> GimmickLaserEmitter::Clone() const {
    auto newObj = std::make_unique<GimmickLaserEmitter>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

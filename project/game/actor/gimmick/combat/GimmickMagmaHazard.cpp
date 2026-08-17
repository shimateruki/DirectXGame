#define NOMINMAX
#include "GimmickMagmaHazard.h"

#include "CollisionConfig.h"
#include "EventManager.h"
#include "Player.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>

void GimmickMagmaHazard::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("MagmaHazard");
    SetName("Gimmick_MagmaHazard");
    SetMaterialType(9);
    SetColor({ 1.0f, 0.24f, 0.015f, 1.0f });
    SetEmissive(1.8f);
    SetRoughness(0.38f);
    SetMetallic(0.0f);
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer);
    // マグマ面は移動しないため、巨大Colliderを静的グリッドへ一度だけ登録する。
    SetStatic(true);

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = { 0.0f, 0.0f, 0.0f };
    collider.size = { 1.0f, 1.0f, 1.0f };
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->speed = 12.0f;
    param_->interval = 0.8f;
}

void GimmickMagmaHazard::Update(float deltaTime) {
    if (!param_.has_value()) {
        param_.emplace();
    }

    const bool isPlaying = SceneManager::GetInstance() && SceneManager::GetInstance()->IsPlaying();
    if (!isPlaying) {
        initializedForPlay_ = false;
        damageCooldownTimer_ = 0.0f;
        SetEmissive(baseEmissive_);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        baseEmissive_ = (std::max)(0.1f, GetEmissive());
        visualTimer_ = 0.0f;
        damageCooldownTimer_ = 0.0f;
        initializedForPlay_ = true;
    }

    damageCooldownTimer_ = (std::max)(0.0f, damageCooldownTimer_ - deltaTime);
    visualTimer_ += deltaTime;
    const float pulse = 0.92f + std::sin(visualTimer_ * 2.6f) * 0.08f;
    SetEmissive(baseEmissive_ * pulse);

    BaseGimmick::Update(deltaTime);
}

bool GimmickMagmaHazard::OnCollision(Object3d* other) {
    if (damageCooldownTimer_ > 0.0f) {
        return true;
    }

    Player* player = dynamic_cast<Player*>(other);
    if (!player) {
        return true;
    }

    const CollisionInfo collision = CheckCollision(player);
    if (!collision.isColliding) {
        return true;
    }

    Vector3 knockback = player->GetWorldPosition() - GetWorldPosition();
    knockback.y = 0.0f;
    if (Math::Length(knockback) < 0.001f) {
        knockback = { 0.0f, 0.0f, 1.0f };
    }
    knockback = Math::Normalize(knockback) * 7.0f;
    knockback.y = 12.0f;

    DamageEvent damageEvent;
    damageEvent.target = player;
    damageEvent.attacker = this;
    damageEvent.damageAmount = GetDamage();
    damageEvent.knockbackVelocity = knockback;
    damageEvent.damageType = DamageType::Fire;
    damageEvent.statusEffect.type = StatusEffectType::Burning;
    damageEvent.statusEffect.duration = 1.4f;
    damageEvent.statusEffect.tickInterval = 0.7f;
    damageEvent.statusEffect.tickDamage = 0.5f;
    EventManager::GetInstance()->Dispatch(damageEvent);

    damageCooldownTimer_ = GetDamageInterval();
    return true;
}

float GimmickMagmaHazard::GetDamage() const {
    return param_.has_value() ? (std::max)(0.0f, param_->speed) : 12.0f;
}

float GimmickMagmaHazard::GetDamageInterval() const {
    return param_.has_value() ? (std::max)(0.1f, param_->interval) : 0.8f;
}

std::unique_ptr<Object3d> GimmickMagmaHazard::Clone() const {
    auto clone = std::make_unique<GimmickMagmaHazard>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

#include "EnemyMushroom.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "EventManager.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kAttackRange = 3.2f;
constexpr float kRangedMinRange = 5.0f;
constexpr float kSporeDamage = 1.0f;
constexpr float kMoveSpeedScale = 1.0f;
constexpr float kCarriedSporeCooldown = 0.65f;
constexpr float kCarriedSporeSpeed = 32.0f;
constexpr float kCarriedSporeLifetime = 2.4f;

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}
}

void EnemyMushroom::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetName("Enemy_Mushroom");
    SetEnemyType("Mushroom");
    SetColor({ 0.86f, 0.18f, 0.24f, 1.0f });
    defaultColor_ = GetColor();

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kGround | kPlayerAttack | kAttributePlayerBullet);
    SetColliderType(ColliderType::kCylinder);
    SetCollisionSize({ 0.8f, 1.1f, 0.8f });
}

void EnemyMushroom::Update(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (isCarried_) {
        return;
    }
    if (IsThrowRecovering()) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        hasBaseScale_ = true;
    }

    idleTimer_ += deltaTime;
    attackCooldown_ = (std::max)(0.0f, attackCooldown_ - deltaTime);

    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    if (target_ && param_.has_value()) {
        Vector3 toTarget = target_->GetTranslate() - GetTranslate();
        Vector3 direction = NormalizePlanar(toTarget);
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);

        UpdateFacing(direction);

        if (attackTimer_ > 0.0f) {
            attackTimer_ -= deltaTime;
            SetColor({ 0.72f, 0.12f, 0.62f, 1.0f });
            if (attackTimer_ <= 0.0f) {
                SetColor(defaultColor_);
            }
        } else if (distance <= kAttackRange && attackCooldown_ <= 0.0f) {
            attackTimer_ = 0.45f;
            attackCooldown_ = 2.0f;
            DispatchSporeDamage(direction, distance);
        } else if (distance <= detectionRange_ && distance >= kRangedMinRange && attackCooldown_ <= 0.0f) {
            attackTimer_ = 0.38f;
            attackCooldown_ = 1.55f;
            FireSporeProjectile(direction, distance);
        } else if (distance <= detectionRange_) {
            const float speed = (std::max)(0.0f, param_->speed) * kMoveSpeedScale;
            float approachSign = distance > kRangedMinRange ? 1.0f : -0.7f;
            velocity.x = direction.x * speed * approachSign;
            velocity.z = direction.z * speed * approachSign;
        } else {
            const float speed = (std::max)(0.55f, param_->speed * 0.42f);
            velocity = CalculateWanderVelocity(deltaTime, speed, 0.65f);
            UpdateFacing({ velocity.x, 0.0f, velocity.z });
        }
    }

    velocity.y = GetVelocity().y;
    SetVelocity(velocity);
    ApplySquashAnimation(deltaTime);
    BaseEnemy::Update(deltaTime);
}

std::unique_ptr<Object3d> EnemyMushroom::Clone() const {
    auto clone = std::make_unique<EnemyMushroom>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    clone->SetTarget(target_);
    clone->SetDetectionRange(detectionRange_);
    return clone;
}

void EnemyMushroom::ExecuteAbility(Player* player) {
    if (!player || !isCarried_ || carriedSporeCooldown_ > 0.0f) {
        return;
    }

    Vector3 direction = player->GetForwardDirection();
    if (Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 0.0f, 1.0f };
    }
    direction = Math::Normalize(direction);

    Vector3 spawnPos = player->GetWorldPosition();
    spawnPos += direction * 1.8f;
    spawnPos.y += 1.45f;

    Vector3 velocity = direction * kCarriedSporeSpeed;
    velocity.y = 2.5f;

    BulletManager::GetInstance()->Fire(
        spawnPos,
        velocity,
        kPlayerAttack,
        kEnemy | kAllSolid,
        "Primitives/sphere",
        0.38f,
        kCarriedSporeLifetime);

    carriedSporeCooldown_ = kCarriedSporeCooldown;
    carriedEffectTimer_ = 0.18f;
    SetColor({ 1.0f, 0.34f, 0.72f, 1.0f });
}

void EnemyMushroom::UpdateCarriedAbility(Player* player, float deltaTime) {
    (void)player;
    if (!isCarried_) {
        return;
    }

    carriedSporeCooldown_ = (std::max)(0.0f, carriedSporeCooldown_ - deltaTime);
    carriedEffectTimer_ = (std::max)(0.0f, carriedEffectTimer_ - deltaTime);

    if (carriedEffectTimer_ > 0.0f) {
        const float pulse = 0.08f + std::sin(carriedEffectTimer_ * 48.0f) * 0.06f;
        SetColor({ 1.0f, 0.34f + pulse, 0.72f, 1.0f });
    } else {
        SetColor(defaultColor_);
    }
}

void EnemyMushroom::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq <= 0.0001f) return;
    const float targetYaw = std::atan2(direction.x, direction.z);
    SetRotationY(Math::LerpShortAngle(GetRotation().y, targetYaw, 0.12f));
}

void EnemyMushroom::DispatchSporeDamage(const Vector3& direction, float distance) {
    if (!target_ || distance > kAttackRange + 0.4f) return;

    DamageEvent damageEvent;
    damageEvent.target = target_;
    damageEvent.attacker = this;
    damageEvent.damageAmount = kSporeDamage;
    damageEvent.knockbackVelocity = { direction.x * 10.0f, 5.0f, direction.z * 10.0f };
    EventManager::GetInstance()->Dispatch(damageEvent);
}

void EnemyMushroom::FireSporeProjectile(const Vector3& direction, float distance) {
    if (!target_) return;

    Vector3 spawnPos = GetTranslate();
    spawnPos.y += 1.15f;

    Vector3 aim = target_->GetTranslate() - spawnPos;
    aim.y += 0.45f;
    if (Math::Length(aim) <= 0.001f) {
        aim = { direction.x, 0.15f, direction.z };
    }
    aim = Math::Normalize(aim);

    const float speed = std::clamp(distance * 1.8f, 14.0f, 26.0f);
    BulletManager::GetInstance()->Fire(
        spawnPos,
        aim * speed,
        kEnemyAttack,
        kPlayer | kAllSolid,
        "Primitives/sphere",
        0.45f,
        3.2f);
}

void EnemyMushroom::ApplySquashAnimation(float deltaTime) {
    Vector3 targetScale = baseScale_;
    if (attackTimer_ > 0.0f) {
        const float pulse = std::sin((0.45f - attackTimer_) * 18.0f) * 0.18f;
        targetScale.x = baseScale_.x * (1.15f + pulse);
        targetScale.y = baseScale_.y * (0.82f - pulse * 0.25f);
        targetScale.z = baseScale_.z * (1.15f + pulse);
    } else {
        const float breathe = std::sin(idleTimer_ * 3.0f) * 0.04f;
        targetScale.x = baseScale_.x * (1.0f + breathe);
        targetScale.y = baseScale_.y * (1.0f - breathe);
        targetScale.z = baseScale_.z * (1.0f + breathe);
    }

    Vector3 scale = GetScale();
    scale = Math::Lerp(scale, targetScale, (std::min)(1.0f, deltaTime * 10.0f));
    SetScale(scale);
}

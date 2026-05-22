#include "EnemyBomber.h"
#include "CollisionConfig.h"
#include "EnemyBomb.h"
#include "EnemyFactory.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinThrowDistance = 1.0f;
constexpr float kBombSpawnHeight = 2.0f;
constexpr float kFootworkSpeed = 1.25f;
constexpr float kPreferredDistance = 13.0f;
}

void EnemyBomber::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    common_ = common;
    throwTimer_ = initialThrowDelay_;
    windupTimer_ = 0.0f;
    footworkTimer_ = 1.2f;
    footworkDirection_ = 1.0f;
    throwState_ = ThrowState::Idle;

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kPlayerAttack);
}

void EnemyBomber::Update(float deltaTime) {
    if (!target_ || isDead) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    float distance = 0.0f;
    Vector3 direction{};
    const bool inRange = IsTargetInRange(&distance, &direction);
    UpdateFacing(direction);
    UpdateFootwork(deltaTime, direction, distance, inRange);

    if (inRange) {
        switch (throwState_) {
        case ThrowState::Idle:
            throwTimer_ -= deltaTime;
            if (throwTimer_ <= 0.0f) {
                BeginThrow();
            }
            break;
        case ThrowState::Windup:
            windupTimer_ -= deltaTime;
            SetColor({ 1.0f, 0.55f, 0.15f, 1.0f });
            if (windupTimer_ <= 0.0f) {
                ThrowBomb();
                throwTimer_ = throwInterval_;
                throwState_ = ThrowState::Idle;
                SetColor(defaultColor_);
            }
            break;
        }
    } else {
        const bool wasWindup = throwState_ == ThrowState::Windup;
        throwState_ = ThrowState::Idle;
        windupTimer_ = 0.0f;
        throwTimer_ = std::min(throwTimer_, initialThrowDelay_);
        if (wasWindup) {
            SetColor(defaultColor_);
        }
    }

    BaseEnemy::Update(deltaTime);
}

bool EnemyBomber::IsTargetInRange(float* outDistance, Vector3* outDirection) const {
    if (!target_) return false;

    Vector3 toTarget = target_->GetTranslate() - GetTranslate();
    toTarget.y = 0.0f;

    const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (outDistance) {
        *outDistance = distance;
    }

    if (distance > 0.001f) {
        toTarget.x /= distance;
        toTarget.z /= distance;
    } else {
        toTarget = { 0.0f, 0.0f, 1.0f };
    }

    if (outDirection) {
        *outDirection = toTarget;
    }

    return distance <= detectionRange_ && distance >= kMinThrowDistance;
}

void EnemyBomber::UpdateFacing(const Vector3& direction) {
    const float lengthSq = direction.x * direction.x + direction.z * direction.z;
    if (lengthSq > 0.0001f) {
        SetRotationY(std::atan2(direction.x, direction.z));
    }
}

void EnemyBomber::UpdateFootwork(float deltaTime, const Vector3& direction, float distance, bool inRange) {
    Vector3 velocity = GetVelocity();
    velocity.x = 0.0f;
    velocity.z = 0.0f;

    if (inRange) {
        footworkTimer_ -= deltaTime;
        if (footworkTimer_ <= 0.0f) {
            footworkDirection_ *= -1.0f;
            footworkTimer_ = 1.4f;
        }

        Vector3 side = { direction.z * footworkDirection_, 0.0f, -direction.x * footworkDirection_ };
        velocity.x += side.x * kFootworkSpeed;
        velocity.z += side.z * kFootworkSpeed;

        if (distance < kPreferredDistance) {
            velocity.x -= direction.x * 0.8f;
            velocity.z -= direction.z * 0.8f;
        } else if (distance > kPreferredDistance + 4.0f) {
            velocity.x += direction.x * 0.6f;
            velocity.z += direction.z * 0.6f;
        }
    }

    SetVelocity(velocity);
}

void EnemyBomber::BeginThrow() {
    throwState_ = ThrowState::Windup;
    windupTimer_ = throwWindup_;
}

void EnemyBomber::ThrowBomb() {
    if (!spawnCallback_ || !common_ || !target_) return;

    auto bomb = EnemyFactory::GetInstance()->CreateEnemy("Bomb", common_);
    if (!bomb) return;

    Vector3 spawnPos = GetTranslate();
    spawnPos.y += kBombSpawnHeight;
    bomb->SetTranslate(spawnPos);
    bomb->SetTarget(target_);

    Vector3 toTarget = target_->GetTranslate() - spawnPos;
    toTarget.y = 0.0f;
    float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (distance > 0.001f) {
        toTarget.x /= distance;
        toTarget.z /= distance;
    } else {
        toTarget = { 0.0f, 0.0f, 1.0f };
    }

    bomb->SetCarried(false);

    const float forwardSpeed = std::clamp(distance * 0.75f, 13.0f, 22.0f);
    const float upSpeed = std::clamp(5.5f + distance * 0.10f, 6.5f, 9.5f);
    bomb->SetVelocity({ toTarget.x * forwardSpeed, upSpeed, toTarget.z * forwardSpeed });

    if (auto* enemyBomb = dynamic_cast<EnemyBomb*>(bomb.get())) {
        enemyBomb->Ignite(2.8f);
    }

    spawnCallback_(std::move(bomb));
}

#include "EnemyBomber.h"
#include "CollisionConfig.h"
#include "EnemyBomb.h"
#include "EnemyFactory.h"
#include "Player.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinThrowDistance = 1.0f;
constexpr float kBombSpawnHeight = 2.0f;
constexpr float kFootworkSpeed = 1.25f;
constexpr float kPreferredDistance = 13.0f;
constexpr float kCarryThrowInterval = 0.42f;
constexpr float kCarryBombForwardSpeed = 24.0f;
constexpr float kCarryBombUpSpeed = 8.0f;
constexpr const char* kCarryBomberThrowEffect = "Resources/json/effect/effect_carry_bomber_throw_burst.json";
constexpr const char* kCarryBomberSparkPreset = "carry_bomber_throw_sparks";
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
    if (isCarried_) {
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (!target_ || isDead) {
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (IsThrowRecovering()) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    float distance = 0.0f;
    Vector3 direction{};
    const bool inRange = IsTargetInRange(&distance, &direction);
    if (inRange) {
        UpdateFacing(direction);
    }
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

void EnemyBomber::SetCarried(bool isCarried) {
    BaseEnemy::SetCarried(isCarried);
    carriedThrowCooldown_ = 0.0f;
    carriedEffectTimer_ = 0.0f;
    throwState_ = ThrowState::Idle;
    windupTimer_ = 0.0f;
    SetColor(defaultColor_);
}

void EnemyBomber::ExecuteAbility(Player* player) {
    if (!player || !isCarried_ || carriedThrowCooldown_ > 0.0f) {
        return;
    }

    ThrowCarryBomb(player);
    carriedThrowCooldown_ = kCarryThrowInterval;
}

void EnemyBomber::UpdateCarriedAbility(Player* player, float deltaTime) {
    (void)player;
    carriedThrowCooldown_ = (std::max)(0.0f, carriedThrowCooldown_ - deltaTime);
    carriedEffectTimer_ = (std::max)(0.0f, carriedEffectTimer_ - deltaTime);

    const float chargeRate = 1.0f - (std::clamp)(carriedThrowCooldown_ / kCarryThrowInterval, 0.0f, 1.0f);
    const float warm = 0.65f + chargeRate * 0.35f;
    SetColor({ 1.0f, warm, 0.55f, 1.0f });
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
    } else {
        velocity = CalculateWanderVelocity(deltaTime, kFootworkSpeed * 0.65f, 0.7f);
        Vector3 wanderDirection = { velocity.x, 0.0f, velocity.z };
        const float lengthSq = wanderDirection.x * wanderDirection.x + wanderDirection.z * wanderDirection.z;
        if (lengthSq > 0.0001f) {
            SetRotationY(Math::LerpShortAngle(GetRotation().y, std::atan2(wanderDirection.x, wanderDirection.z), 0.08f));
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

void EnemyBomber::ThrowCarryBomb(Player* player) {
    if (!player || !common_) {
        return;
    }

    auto bomb = EnemyFactory::GetInstance()->CreateEnemy("Bomb", common_);
    if (!bomb) {
        return;
    }

    const Vector3 forward = GetPlayerForward(player);
    const Vector3 playerPos = player->GetWorldPosition();
    Vector3 spawnPos = {
        playerPos.x + forward.x * 2.1f,
        playerPos.y + 2.15f,
        playerPos.z + forward.z * 2.1f
    };

    bomb->SetTranslate(spawnPos);
    bomb->SetRotationY(std::atan2(forward.x, forward.z));
    bomb->SetTarget(target_ ? target_ : player);
    bomb->SetCarried(false);
    bomb->SetVelocity({
        forward.x * kCarryBombForwardSpeed,
        kCarryBombUpSpeed,
        forward.z * kCarryBombForwardSpeed
    });

    if (auto* enemyBomb = dynamic_cast<EnemyBomb*>(bomb.get())) {
        enemyBomb->Ignite(2.25f);
    }

    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            kCarryBomberThrowEffect,
            spawnPos,
            { 0.0f, std::atan2(forward.x, forward.z), 0.0f },
            { 1.0f, 1.0f, 1.0f }
        );
    }
    if (auto* gpuParticleManager = GPUParticleManager::GetInstance(); gpuParticleManager->IsInitialized()) {
        gpuParticleManager->Emit(kCarryBomberSparkPreset, spawnPos);
    }

    SpawnBombObject(std::move(bomb));
}

void EnemyBomber::SpawnBombObject(std::unique_ptr<BaseEnemy> bomb) {
    if (!bomb) {
        return;
    }

    if (spawnCallback_) {
        spawnCallback_(std::move(bomb));
        return;
    }

    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager || !sceneManager->GetCurrentScene()) {
        return;
    }

    if (!bomb->IsCarried()) {
        bomb->SetTarget(sceneManager->GetCurrentScene()->GetPlayer());
    }
    sceneManager->GetCurrentScene()->AddObject(std::move(bomb));
}

Vector3 EnemyBomber::GetPlayerForward(Player* player) const {
    const Vector3 rotation = player->GetRotation();
    Vector3 forward = { std::sin(rotation.y), 0.0f, std::cos(rotation.y) };
    const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { forward.x / length, 0.0f, forward.z / length };
}

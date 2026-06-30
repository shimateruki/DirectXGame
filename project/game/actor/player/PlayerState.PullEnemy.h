#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

class Object3d;

class PlayerStatePullEnemy : public IAnimationState
{
public:
    PlayerStatePullEnemy(Object3d* targetEnemy, const Vector3& targetPos)
        : targetEnemy_(targetEnemy), targetPos_(targetPos) {
    }
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

private:
    enum class Phase {
        kShootHook,
        kPullEnemy
    };

    Object3d* targetEnemy_ = nullptr;
    Vector3 targetPos_;
    Vector3 hookTipPos_;
    Phase phase_ = Phase::kShootHook;
    Vector3 enemyStartPos_;
    Vector3 enemyPullStartScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 enemyBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 enemyBaseRotation_ = { 0.0f, 0.0f, 0.0f };
    Quaternion enemyBaseQuaternion_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool enemyBaseQuaternionMaster_ = true;
    float pullTimer_ = 0.0f;
    bool isHeavyPullTarget_ = false;
    Vector3 heavyPullBasePlayerPos_ = { 0.0f, 0.0f, 0.0f };
    bool hasHeavyPullBasePlayerPos_ = false;
};

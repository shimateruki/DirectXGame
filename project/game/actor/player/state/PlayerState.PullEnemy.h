#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

class Object3d;

/// <summary>吸い込み対象の敵をフックで引き寄せ、元の変形情報を保持します。</summary>
class PlayerStatePullEnemy : public IAnimationState {
  public:
    PlayerStatePullEnemy(Object3d* targetEnemy, const Vector3& targetPos)
        : targetEnemy_(targetEnemy), targetPos_(targetPos) {}
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

  private:
    enum class Phase { kShootHook, kPullEnemy };

    // 対象の生存期間は呼び出し元が管理します。終了時には元の変形情報を復元します。
    Object3d* targetEnemy_ = nullptr;
    Vector3 targetPos_;
    Vector3 hookTipPos_;
    Phase phase_ = Phase::kShootHook;
    Vector3 enemyStartPos_;
    Vector3 enemyPullStartScale_ = {1.0f, 1.0f, 1.0f};
    Vector3 enemyBaseScale_ = {1.0f, 1.0f, 1.0f};
    Vector3 enemyBaseRotation_ = {0.0f, 0.0f, 0.0f};
    Quaternion enemyBaseQuaternion_ = {0.0f, 0.0f, 0.0f, 1.0f};
    bool enemyBaseQuaternionMaster_ = true;
    float pullTimer_ = 0.0f;
    bool isHeavyPullTarget_ = false;
    Vector3 heavyPullBasePlayerPos_ = {0.0f, 0.0f, 0.0f};
    bool hasHeavyPullBasePlayerPos_ = false;
    Vector3 normalPullBasePlayerPos_ = {0.0f, 0.0f, 0.0f};
    bool hasNormalPullBasePlayerPos_ = false;
};

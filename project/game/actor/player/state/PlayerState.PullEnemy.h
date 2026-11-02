#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

namespace engine { class Object3d; }

/// <summary>吸い込み対象の敵をフックで引き寄せ、元の変形情報を保持します。</summary>
class PlayerStatePullEnemy : public IAnimationState {
  public:
    PlayerStatePullEnemy(engine::Object3d* targetEnemy, const engine::Vector3& targetPos, bool alreadyHit = false)
        : targetEnemy_(targetEnemy), targetPos_(targetPos), alreadyHit_(alreadyHit) {}

    // 自身の操作と移動を止め、対象の姿勢を退避して引き寄せを開始する。
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    // 操作を戻し、引き寄せ用のマーカーや退避した対象の姿勢を後始末する。
    void Exit(Player* player) override;

  private:
    bool alreadyHit_ = false;
    enum class Phase { kShootHook, kPullEnemy };

    // 対象の生存期間は呼び出し元が管理します。終了時には元の変形情報を復元します。
    engine::Object3d* targetEnemy_ = nullptr;
    engine::Vector3 targetPos_;
    engine::Vector3 hookTipPos_;

    Phase phase_ = Phase::kShootHook;

    engine::Vector3 enemyStartPos_;
    engine::Vector3 enemyPullStartScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 enemyBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 enemyBaseRotation_ = engine::Vector3::ZERO;
    engine::Quaternion enemyBaseQuaternion_ = {0.0f, 0.0f, 0.0f, 1.0f};
    bool enemyBaseQuaternionMaster_ = true;

    float pullTimer_ = 0.0f;

    bool isHeavyPullTarget_ = false;
    engine::Vector3 heavyPullBasePlayerPos_ = engine::Vector3::ZERO;
    bool hasHeavyPullBasePlayerPos_ = false;

    engine::Vector3 normalPullBasePlayerPos_ = engine::Vector3::ZERO;
    bool hasNormalPullBasePlayerPos_ = false;
};

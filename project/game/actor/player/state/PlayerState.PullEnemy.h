#pragma once

#include "IAnimationState.h"
#include <memory>
#include <utility>
class PlayerWhipVisual;
#include "engine/utility/math/Math.h"

namespace engine { class Object3D; }

/// <summary>吸い込み対象の敵をフックで引き寄せ、元の変形情報を保持します。</summary>
// 関連：PlayerStateWhipで捕捉した敵を引き寄せ、保持状態へ受け渡す。
// 主な入口：Enter / Update / Exit
class PlayerStatePullEnemy : public IAnimationState {
  public:
    PlayerStatePullEnemy(engine::Object3D* targetEnemy, const engine::Vector3& targetPos, bool alreadyHit = false,
                         std::shared_ptr<PlayerWhipVisual> whipVisual = {})
        : targetEnemy_(targetEnemy), targetPos_(targetPos), alreadyHit_(alreadyHit), whipVisual_(std::move(whipVisual)) {}

    // 自身の操作と移動を止め、対象の姿勢を退避して引き寄せを開始する。
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    // 操作を戻し、引き寄せ用のマーカーや退避した対象の姿勢を後始末する。
    void Exit(Player* player) override;

  private:
    bool alreadyHit_ = false;
    std::shared_ptr<PlayerWhipVisual> whipVisual_;
    enum class Phase { ShootHook, PullEnemy };

    // 対象の生存期間は呼び出し元が管理します。終了時には元の変形情報を復元します。
    engine::Object3D* targetEnemy_ = nullptr;
    engine::Vector3 targetPos_;
    engine::Vector3 hookTipPos_;

    Phase phase_ = Phase::ShootHook;

    engine::Vector3 enemyStartPos_;
    engine::Vector3 enemyPullStartScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 enemyBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 enemyBaseRotation_ = engine::Vector3::ZERO;
    engine::Quaternion enemyBaseQuaternion_ = {0.0f, 0.0f, 0.0f, 1.0f};
    bool enemyBaseQuaternionMaster_ = true;

    float pullTimer_ = 0.0f; // 引き寄せに使う経過秒数。

    bool isHeavyPullTarget_ = false;
    engine::Vector3 heavyPullBasePlayerPos_ = engine::Vector3::ZERO;
    bool hasHeavyPullBasePlayerPos_ = false;

    engine::Vector3 normalPullBasePlayerPos_ = engine::Vector3::ZERO;
    bool hasNormalPullBasePlayerPos_ = false;
};

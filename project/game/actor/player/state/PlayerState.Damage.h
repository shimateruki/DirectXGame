#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

/// <summary>被弾時のノックバックと姿勢の復元を管理します。</summary>
// 関連：Playerの被ダメージ時に入り、通常操作へ戻るまでの硬直を扱う。
// 主な入口：Enter / Update / Exit
class PlayerStateDamage : public IAnimationState {
  public:
    explicit PlayerStateDamage(const engine::Vector3& knockbackDir) : knockbackDir_(knockbackDir) {}

    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    // ノックバックの変形を次状態へ持ち越さず、入場前の姿勢へ戻す。
    void Exit(Player* player) override;

  private:
    engine::Vector3 knockbackDir_;
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 baseRotation_ = engine::Vector3::ZERO;
    float timer_ = 0.0f;

    // 被弾状態を継続する秒数。
    const float duration_ = 0.58f;
};

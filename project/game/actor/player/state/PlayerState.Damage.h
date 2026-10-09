#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

/// <summary>被弾時のノックバックと姿勢の復元を管理します。</summary>
class PlayerStateDamage : public IAnimationState {
  public:
    explicit PlayerStateDamage(const Vector3& knockbackDir) : knockbackDir_(knockbackDir) {}
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

  private:
    Vector3 knockbackDir_;
    Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    Vector3 baseRotation_ = {0.0f, 0.0f, 0.0f};
    float timer_ = 0.0f;
    // 被弾状態を継続する秒数。
    const float duration_ = 0.58f;
};

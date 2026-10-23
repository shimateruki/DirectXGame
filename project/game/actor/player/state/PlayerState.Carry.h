#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

/// <summary>敵を持ち運んでいる間の姿勢と、運搬対象の変形の復元を管理します。</summary>
class PlayerStateCarry : public IAnimationState {
  public:
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    // 運搬中に変えた敵の縮尺を戻す。投げた後の状態へ変形を持ち越さない。
    void Exit(Player* player) override;

  private:
    float struggleTimer_ = 0.0f;
    engine::Vector3 carriedBaseScale_ = {1.0f, 1.0f, 1.0f};
};

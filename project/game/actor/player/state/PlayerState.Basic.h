#pragma once

#include "IAnimationState.h"

/// <summary>入力待機中の姿勢と待機アニメーションを管理します。</summary>
class PlayerStateIdle : public IAnimationState {
  public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;
};

/// <summary>地上移動中の姿勢と走行アニメーションを管理します。</summary>
class PlayerStateRun : public IAnimationState {
  public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;
};

/// <summary>ジャンプ中のアニメーション状態を管理します。</summary>
class PlayerStateJump : public IAnimationState {
  public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;
};

/// <summary>ダッシュ中の演出を管理し、継続時間を追跡します。</summary>
class PlayerStateDash : public IAnimationState {
  public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

  private:
    float timer_ = 0.0f;
};

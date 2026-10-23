#pragma once

#include "IAnimationState.h"

/// <summary>入力待機中の姿勢と待機アニメーションを管理します。</summary>
class PlayerStateIdle : public IAnimationState {
  public:
    // 待機姿勢へ切り替え、直前のジャンプ溜め表示を解除する。
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    void Exit(Player* player) override;
};

/// <summary>地上移動中の姿勢と走行アニメーションを管理します。</summary>
class PlayerStateRun : public IAnimationState {
  public:
    // 走行姿勢へ切り替える。移動速度の決定は Player の移動処理が担当する。
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    void Exit(Player* player) override;
};

/// <summary>ジャンプ中のアニメーション状態を管理します。</summary>
class PlayerStateJump : public IAnimationState {
  public:
    // 空中の姿勢をアニメーターへ渡す。物理ジャンプの発動とは区別する。
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    void Exit(Player* player) override;
};

/// <summary>ダッシュ中の演出を管理し、継続時間を追跡します。</summary>
class PlayerStateDash : public IAnimationState {
  public:
    // 演出時計を初期化し、ダッシュ方向を身体の変形へ渡す。
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    // 弾性の反動を与え、次の状態の姿勢へつなぐ。
    void Exit(Player* player) override;

  private:
    float timer_ = 0.0f;
};

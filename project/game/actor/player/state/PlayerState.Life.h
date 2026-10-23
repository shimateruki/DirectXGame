#pragma once

#include "IAnimationState.h"
#include "game/actor/player/animation/PlayerDeathAnimation.h"
#include "engine/utility/math/Math.h"

/// <summary>死亡演出を再生し、残機に応じて次のシーンへ進めます。</summary>
class PlayerStateDead : public IAnimationState {
  public:
    // 死亡アニメーションを開始し、残機に応じた暗転と遷移の一度きりのフラグを初期化する。
    void Enter(Player* player) override;

    // 死亡演出→暗転→残機表示の完了を待ち、再開またはゲームオーバーへ一度だけ遷移する。
    void Update(Player* player, float deltaTime) override;

    // 見た目と操作を戻し、演出用カメラの上書きと位置固定を解除する。
    void Exit(Player* player) override;

  private:
    float timer_ = 0.0f;
    bool sceneChangeRequested_ = false;
    bool lifePresentationStarted_ = false;
    bool finalDeath_ = false;
    engine::Vector2 irisCenter_ = {0.5f, 0.5f};
    PlayerDeathAnimation deathAnimation_;
};

/// <summary>落下時の残機減少、待機、アイリス演出を順番に処理します。</summary>
class PlayerStateFallingOut : public IAnimationState {
  public:
    // 落下直後の投影位置を暗転の中心に固定し、入力を止めて残機減少の段階へ入る。
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    void Exit(Player* player) override;

  private:
    enum class Phase { LifeLost, Waiting, IrisOut, IrisIn };

    Phase phase_ = Phase::Waiting;
    float waitTimer_ = 0.0f;
    engine::Vector2 irisCenter_ = {0.5f, 0.5f};
};

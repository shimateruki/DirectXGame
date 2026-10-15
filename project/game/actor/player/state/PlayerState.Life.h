#pragma once

#include "IAnimationState.h"
#include "game/actor/player/animation/PlayerDeathAnimation.h"
#include "engine/utility/math/Math.h"

/// <summary>死亡演出を再生し、残機に応じて次のシーンへ進めます。</summary>
class PlayerStateDead : public IAnimationState {
  public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
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
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

  private:
    enum class Phase { LifeLost, Waiting, IrisOut, IrisIn };

    Phase phase_ = Phase::Waiting;
    float waitTimer_ = 0.0f;
    engine::Vector2 irisCenter_ = {0.5f, 0.5f};
};

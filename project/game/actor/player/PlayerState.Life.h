#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

class PlayerStateDead : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    float timer_ = 0.0f;
    bool sceneChangeRequested_ = false;
    bool lifePresentationStarted_ = false;
    Vector2 irisCenter_ = { 0.5f, 0.5f };
};

class PlayerStateFallingOut : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    enum class Phase {
        LifeLost,
        Waiting,
        IrisOut,
        IrisIn
    };

    Phase phase_ = Phase::Waiting;
    float waitTimer_ = 0.0f;
    Vector2 irisCenter_ = { 0.5f, 0.5f };
};

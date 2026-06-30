#pragma once

#include "IAnimationState.h"

class PlayerStateIdle : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;
};

class PlayerStateRun : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;
};

class PlayerStateJump : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;
};

class PlayerStateDash : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

private:
    float timer_ = 0.0f;
};

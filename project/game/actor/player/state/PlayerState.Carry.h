#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

class PlayerStateCarry : public IAnimationState
{
public:
    void Enter(Player* player) override;
    void Update(Player* player, float deltaTime) override;
    void Exit(Player* player) override;

private:
    float struggleTimer_ = 0.0f;
    Vector3 carriedBaseScale_ = { 1.0f, 1.0f, 1.0f };
};

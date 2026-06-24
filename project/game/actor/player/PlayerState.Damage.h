#pragma once

#include "IAnimationState.h"
#include "engine/utility/math/Math.h"

class PlayerStateDamage : public IAnimationState
{
public:
    explicit PlayerStateDamage(const Vector3& knockbackDir) : knockbackDir_(knockbackDir) {}
    void Enter(Player* player) override;
    void Update(Player* player) override;
    void Exit(Player* player) override;

private:
    Vector3 knockbackDir_;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    float timer_ = 0.0f;
    const float duration_ = 0.58f;
};

#pragma once
#include "IMoveStrategy.h"

class MoveStrategy2D : public IMoveStrategy {
public:
    Vector3 CalculateVelocity(Player* player) override;
private:
    Math math;
};
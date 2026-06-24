#pragma once
#include "IMoveStrategy.h"

// カメラ方向を基準に、3D 空間で自由移動するためのプレイヤー移動戦略
class MoveStrategy3D : public IMoveStrategy {
public:
    Vector3 CalculateVelocity(Player* player) override;
private:
    Math math; 
};

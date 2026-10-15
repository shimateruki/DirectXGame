#pragma once
#include "IMoveStrategy.h"

// 横スクロール風の 2D 入力に制限するためのプレイヤー移動戦略
class MoveStrategy2D : public IMoveStrategy {
  public:
    engine::Vector3 CalculateVelocity(Player* player) override;

  private:
    engine::Math math;
};

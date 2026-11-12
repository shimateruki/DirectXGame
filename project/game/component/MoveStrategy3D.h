#pragma once
#include "IMoveStrategy.h"

// カメラ方向を基準に、3D 空間で自由移動するためのプレイヤー移動戦略
// 関連：PlayerMoverから入力とカメラ方向を受け、水平面の移動速度を返す。
// 主な入口：CalculateVelocity
class MoveStrategy3D : public IMoveStrategy {
  public:
    engine::Vector3 CalculateVelocity(Player* player) override;

  private:
    engine::Math math_;
};

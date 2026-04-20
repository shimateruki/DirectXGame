#pragma once
#include "BaseEnemy.h"

// スライム型の敵クラス
class EnemySlime : public BaseEnemy {
public:
 

    // 動きを上書きする
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
};
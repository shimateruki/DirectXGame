#pragma once
#include "BaseEnemy.h"

// スライム型の敵クラス
class EnemySlime : public BaseEnemy {
public:
 

    // 動きを上書きする
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ExecuteAbility(class Player* player) override;
private:
    float jumpTimer_ = 0.0f;     // 次に跳ねるまでの待ち時間
    bool isHopping_ = false;     // 空中に跳ねている最中か
};
#pragma once
#include "BaseEnemy.h"

// 地上を跳ねながらプレイヤーへ近づく、基本スライム敵
class EnemySlime : public BaseEnemy {
public:
 

    // 跳躍AIとスライムらしい伸縮を更新する
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ExecuteAbility(class Player* player) override;
private:
    float jumpTimer_ = 0.0f;     // 次に跳ねるまでの待ち時間。
    bool isHopping_ = false;     // 空中に跳ねている最中か。
    Vector3 baseScale_ = { 2.0f, 2.0f, 2.0f };
    bool hasBaseScale_ = false;
};

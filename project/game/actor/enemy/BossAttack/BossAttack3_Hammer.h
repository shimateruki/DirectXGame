#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class BossAttack3_Hammer : public BaseBossAttack {
private:
    Vector3 animStartPos_ = { 0,0,0 };
    Vector3 animTargetPos_ = { 0,0,0 };
    Vector3 animStartRot_ = { 0,0,0 }; // ハンマーは角度の記憶が必要
    Vector3 attackDir_ = { 0,0,1 };    // 攻撃開始時のプレイヤー方向
    std::vector<Vector3> blockStartPos_;
    int attackCount_ = 0; // コンボ攻撃の回数カウント

public:
    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
};
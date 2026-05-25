#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class BossAttack2_Shoot : public BaseBossAttack {
private:
    // モード2だけで使う専用変数
    Vector3 animStartPos_ = { 0,0,0 };
    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;

    int shotCount_ = 0; // 撃った数も自分が管理する
    float descendTimer_ = 0.0f; // 徐々に下に下がるためのタイマー

public:
    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
};
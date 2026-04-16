#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class BossAttack1_Rush : public BaseBossAttack {
private:
    // モード1だけで使う変数をここに書く
    Vector3 animStartPos_ = { 0,0,0 };
    Vector3 animTargetPos_ = { 0,0,0 };
    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;

public:
    // 親クラスの関数を上書き（override）する
    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
};
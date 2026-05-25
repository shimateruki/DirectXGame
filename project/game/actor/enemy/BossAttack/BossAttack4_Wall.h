#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class BossAttack4_Wall : public BaseBossAttack {
private:
    Vector3 animStartPos_ = { 0,0,0 };
    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;

    // 今まで shotCount_ にやらせていた往復回数のカウントを自前で持つ！
    int wallStep_ = 0;
    BossCore* boss_ = nullptr;

public:
    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
    void Finalize() override;
};
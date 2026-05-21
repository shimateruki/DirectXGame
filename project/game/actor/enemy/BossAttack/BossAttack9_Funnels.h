#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class BossAttack9_Funnels : public BaseBossAttack {
public:
    BossAttack9_Funnels() = default;
    ~BossAttack9_Funnels() override { Finalize(); }

    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
    void Finalize() override;

private:
    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;
    std::vector<Vector3> blockStartScale_;
    
    // 各ファンネルに割り当てるレーザーオブジェクト
    std::vector<class Object3d*> activeLasers_;
    std::vector<class Object3d*> activeCoreLasers_;

    std::vector<int> funnelStates_;
    std::vector<float> funnelTimers_;
    std::vector<int> funnelFireCounts_;
};
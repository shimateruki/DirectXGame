#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class BossAttack5_Humanoid : public BaseBossAttack {
private:
    Vector3 animStartPos_ = { 0,0,0 };
    Vector3 animStartRot_ = { 0,0,0 };
    
    float deformationSeTimer_ = 0.0f;
    float lastWalkCycle_ = 0.0f;

    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;
    std::vector<Vector3> blockStartScale_;
    std::vector<Vector3> blockTargetScale_;

public:
    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
};
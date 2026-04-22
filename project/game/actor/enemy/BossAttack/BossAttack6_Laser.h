#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class Object3d;

class BossAttack6_Laser : public BaseBossAttack {
private:
    Vector3 animStartPos_ = { 0,0,0 };

    Vector3 animStartRot_ = { 0.0f, 0.0f, 0.0f };

    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;
    std::vector<Vector3> blockStartScale_;
    std::vector<Vector3> blockTargetScale_;
    std::vector<Vector3> attentionStartRot_;

    // ==========================================
    // 発射の瞬間に生み出したレーザーを管理するリスト
    // ==========================================
    std::vector<Object3d*> activeBeams_;

public:
    // ==========================================
    // 攻撃が中断されても絶対にレーザーを消し去る安全装置！
    // ==========================================
    ~BossAttack6_Laser();

    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
    void Finalize() override;
};
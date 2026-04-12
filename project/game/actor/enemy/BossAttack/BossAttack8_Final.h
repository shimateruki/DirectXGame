#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class Object3d;

class BossAttack8_Final : public BaseBossAttack {
private:
    Vector3 animStartPos_ = { 0,0,0 };

    // 生成した隕石（ブロック）を管理するリスト
    std::vector<Object3d*> meteors_;

    float rainTimer_ = 0.0f;
    int rainCount_ = 0;

public:
    // 安全装置：途中でゲームが終わっても隕石を確実に消す
    ~BossAttack8_Final();

    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
};
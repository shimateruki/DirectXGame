#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"

class BossAttack7_Absorb : public BaseBossAttack {
private:
    Vector3 animStartPos_ = { 0,0,0 };

public:
    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
};
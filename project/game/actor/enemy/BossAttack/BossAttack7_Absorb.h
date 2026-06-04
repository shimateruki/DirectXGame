#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"

class MapBlock;

class BossAttack7_Absorb : public BaseBossAttack {
private:
    Vector3 animStartPos_ = { 0,0,0 };

    // ロックオンしたブロックだけを入れる専用リスト！
    std::vector<MapBlock*> targetMapBlocks_;

public:
    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
};
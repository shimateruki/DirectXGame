#pragma once
#include "BaseBossAttack.h"
#include "engine/utility/math/Math.h"
#include <vector>

class BossAttack9_Spawn : public BaseBossAttack {
private:
    int animPhase_ = 0;       // 演出フェーズ管理
    int spawnCount_ = 0;      // 現在までに召喚した数
    float spawnTimer_ = 0.0f; // 召喚間隔を測るタイマー

    // 内部的な召喚処理
    void SpawnEnemy(BossCore* boss);

public:
    // 基本のオーバーライド
    void Initialize(BossCore* boss) override;
    void Update(BossCore* boss, float deltaTime) override;
};
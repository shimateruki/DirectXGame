#pragma once
#include <vector>
#include "Math.h" // Vector3などのため

class BossCore; // ボス本体を操作するために前方宣言

class BaseBossAttack {
protected:
    int animPhase_ = 0;
    float animTimer_ = 0.0f;
    bool isFinished_ = false; // 攻撃が終わったかどうかのフラグ

public:
    virtual ~BaseBossAttack() = default;

    // 攻撃が選ばれた瞬間に呼ばれる
    virtual void Initialize(BossCore* boss) {
        animPhase_ = 0;
        animTimer_ = 0.0f;
        isFinished_ = false;
    }

    // 毎フレーム呼ばれる（継承先で必ず実装する）
    virtual void Update(BossCore* boss, float deltaTime) = 0;

    // ボス本体に「攻撃が終わったよ」と伝えるための関数
    bool IsFinished() const { return isFinished_; }
};
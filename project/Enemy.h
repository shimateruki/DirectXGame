#pragma once
#include "Character.h"

class Enemy : public Character {
public:
    // 初期化
    void Initialize(Object3dCommon* common, const Vector3& position);

    // 更新
    void Update(float deltaTime) override;

    // ダメージを受ける関数
    void OnHit(int damage);

    // 生きているか確認
    bool IsDead() const { return isDead_; }

    void Draw() override;
    int GetHP() { return hp_; }

private:
    int hp_ = 3;           // 体力
    bool isDead_ = false;  // 死亡フラグ
    float invincibleTimer_ = 0.0f;
    // 無敵時間の長さ（秒）
    const float kInvincibleTime_ = 0.5f;
};
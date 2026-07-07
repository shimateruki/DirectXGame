#pragma once
#include "BaseEnemy.h"

// 中距離から胞子弾を撃ち、持ち運び中はプレイヤーの補助射撃になる敵
class EnemyMushroom : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ExecuteAbility(class Player* player) override;
    void UpdateCarriedAbility(class Player* player, float deltaTime) override;

private:
    // 向き・胞子攻撃・見た目の潰れ戻りを小さな処理に分けている
    bool UpdateInactiveState(float deltaTime);
    void EnsureBaseScale();
    void UpdateTimers(float deltaTime);
    void UpdateWildBehavior(float deltaTime, Vector3& velocity);
    void UpdateCombatBehavior(float deltaTime, Vector3& velocity, const Vector3& direction, float distance);
    void UpdateWanderBehavior(float deltaTime, Vector3& velocity);
    void UpdateFacing(const Vector3& direction);
    void DispatchSporeDamage(const Vector3& direction, float distance);
    void FireSporeProjectile(const Vector3& direction, float distance);
    void ApplySquashAnimation(float deltaTime);

    float attackCooldown_ = 0.8f;        // 野生時の次弾までの待ち時間。
    float carriedSporeCooldown_ = 0.0f;  // 持ち運び能力の再使用待ち。
    float carriedEffectTimer_ = 0.0f;    // 発射直後の発光演出タイマー。
    float attackTimer_ = 0.0f;           // 攻撃モーション中の伸縮制御。
    float idleTimer_ = 0.0f;             // 待機呼吸アニメーション用。
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    bool hasBaseScale_ = false;
};

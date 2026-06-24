#pragma once
#include "BaseEnemy.h"

// 近距離では炎ブレス、距離がある時は火球で攻撃する属性スライム
class EnemyFireSlime : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void BeginThrown(const Vector3& initialVelocity) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ExecuteAbility(class Player* player) override;
    void ExecuteBreathAbility(class Player* player);
    void UpdateCarriedAbility(class Player* player, float deltaTime) override;
    void ReleaseCarriedAbilityVisuals();

private:
    // 攻撃本体、火の演出、スライムの伸縮を分けて管理する
    void UpdateFacing(const Vector3& direction);
    void StartBreath();
    void UpdateBreath(float deltaTime, const Vector3& direction, float distance);
    void DispatchBreathDamage(const Vector3& direction, float distance);
    void DispatchCarriedBreathDamage(class Player* player, const Vector3& direction);
    void FireFireball(const Vector3& direction, float distance);
    void UpdateHeadFlame(float deltaTime);
    void EnsureHeadFlameVisual();
    void UpdateHeadFlameVisual(float deltaTime);
    void RequestRemoveHeadFlameVisual();
    void EnsureBreathFlameVisuals();
    void UpdateBreathFlameVisuals(const Vector3& direction, float deltaTime);
    void HideBreathFlameVisuals();
    void RequestRemoveBreathFlameVisuals();
    void EmitFirePreset(const char* presetName, const Vector3& position);
    void ApplySlimeAnimation(float deltaTime);
    void SyncWorldCollisionRadius(float worldRadius);
    void SyncGroundCollisionRadius();
    void SyncThrownCollisionRadius();

    float attackCooldown_ = 0.65f;       // 野生時の攻撃再使用待ち。
    float breathTimer_ = 0.0f;           // ブレス持続時間。
    float breathParticleTimer_ = 0.0f;   // ブレス粒子の発生間隔。
    float attackTimer_ = 0.0f;           // 攻撃中の伸縮演出。
    float idleTimer_ = 0.0f;             // 待機呼吸アニメーション用。
    float carriedFireCooldown_ = 0.0f;   // 持ち運び能力の再使用待ち。
    float carriedEffectTimer_ = 0.0f;    // 発射直後の発光演出。
    float carriedBreathDamageTimer_ = 0.0f; // 吸収中ブレスの連続ヒット間隔。
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Object3d* headFlameVisual_ = nullptr;
    Object3d* breathFlameVisuals_[4] = {};
    bool hasBaseScale_ = false;
    bool breathDamageDone_ = false;
    bool headFlameRemoveRequested_ = false;
    bool breathFlameRemoveRequested_ = false;
};

#pragma once
#include "BaseEnemy.h"

// 近距離では炎ブレス、距離がある時は火球で攻撃する属性スライム
// EnemyFireSlimeは、炎ブレス、火球、頭上炎エフェクトを使う属性スライム敵です。
class EnemyFireSlime : public BaseEnemy {
public:
        // 炎スライム用モデル、Collider、初期能力値、炎エフェクトを準備します。
void Initialize(Object3dCommon* common, const std::string& modelName) override;
        // 徘徊、追跡、炎攻撃、投げ復帰、炎見た目を更新します。
void Update(float deltaTime) override;
    void BeginThrown(const Vector3& initialVelocity) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ApplyManagedScale(const Vector3& scale) override {
        baseScale_ = scale;
        hasBaseScale_ = true;
        SetScale(scale);
    }

private:
    // 攻撃本体、火の演出、スライムの伸縮を分けて管理する
    bool UpdateInactiveState(float deltaTime);
    bool UpdateThrowRecoveryState(float deltaTime);
    void EnsureBaseScale();
    void UpdateWildTimers(float deltaTime);
    void UpdateWildBehavior(float deltaTime, Vector3& velocity);
    void UpdateCombatBehavior(float deltaTime, Vector3& velocity, const Vector3& direction, float distance);
    void UpdateWanderBehavior(float deltaTime, Vector3& velocity);
    void ApplyGroundMovementAndAnimation(float deltaTime, Vector3& velocity);
    void UpdateFacing(const Vector3& direction);
        // 炎ブレス攻撃の予兆と再生状態を開始します。
void StartBreath();
    void UpdateBreath(float deltaTime, const Vector3& direction, float distance);
    void StartFireballWindup(const Vector3& direction, float distance);
    void UpdateFireballWindup(float deltaTime, Vector3& velocity, const Vector3& direction, float distance);
        // ブレス範囲内の対象へダメージを通知します。
void DispatchBreathDamage(const Vector3& direction, float distance);
    void FireFireball(const Vector3& direction, float distance);
    void UpdateHeadFlame(float deltaTime);
    void EnsureHeadFlameVisual();
        // 頭上の炎エフェクトを本体位置と状態へ同期します。
    void UpdateHeadFlameVisual(float deltaTime);
    void RequestRemoveHeadFlameVisual();
    void EnsureBreathFlameVisuals();
    void UpdateBreathFlameVisuals(const Vector3& direction, float deltaTime);
    void HideBreathFlameVisuals();
    void RequestRemoveBreathFlameVisuals();
    void EmitBreathParticles(const Vector3& origin, const Vector3& direction);
    void EmitFirePreset(const char* presetName, const Vector3& position);
    void EmitDirectedFirePreset(const char* presetName, const Vector3& position, const Vector3& direction, float speedScale = 1.0f);
    void ApplySlimeAnimation(float deltaTime);
    void SyncWorldCollisionRadius(float worldRadius);
    void SyncGroundCollisionRadius();
    void SyncThrownCollisionRadius();

    float attackCooldown_ = 0.65f;       // 野生時の攻撃再使用待ち。
    float breathTimer_ = 0.0f;           // ブレス持続時間。
    float fireballWindupTimer_ = 0.0f;   // 火球を撃つ前に見せる溜め時間。
    float breathParticleTimer_ = 0.0f;   // ブレス粒子の発生間隔。
    float breathEmberTimer_ = 0.0f;      // ブレスの火の粉発生間隔。
    float headFlameParticleTimer_ = 0.0f;
    float headEmberParticleTimer_ = 0.0f;
    float attackTimer_ = 0.0f;           // 攻撃中の伸縮演出。
    float idleTimer_ = 0.0f;             // 待機呼吸アニメーション用。
    float groundHopTimer_ = 0.0f;        // 通常移動中の小ホップ周期。
    float pendingFireballDistance_ = 0.0f;
    Vector3 pendingFireballDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector3 smoothedFlameVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Object3d* headFlameVisual_ = nullptr;
    Object3d* breathFlameVisuals_[7] = {};
    int breathParticleCursor_ = 0;
    int headFlameParticleCursor_ = 0;
    bool hasBaseScale_ = false;
    bool breathDamageDone_ = false;
    bool breathWarningTriggered_ = false;
    bool fireballWarningTriggered_ = false;
    bool fireballAimLocked_ = false;
    bool headFlameRemoveRequested_ = false;
    bool breathFlameRemoveRequested_ = false;
};

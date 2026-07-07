#pragma once
#include "BaseEnemy.h"
#include "EffectObject3d.h"
#include <memory>

// 地面に広がる放電と常時オーラで攻撃範囲を見せる雷スライム
// EnemyThunderSlimeは、チャージ放電、雷オーラ、待機火花を使う属性スライム敵です。
class EnemyThunderSlime : public BaseEnemy {
public:
    ~EnemyThunderSlime() override;
        // 雷スライム用モデル、Collider、雷演出の初期状態を準備します。
void Initialize(Object3dCommon* common, const std::string& modelName) override;
        // 徘徊、追跡、チャージ、放電、雷オーラを更新します。
void Update(float deltaTime) override;
        // 本体と雷オーラをライト情報付きで描画します。
void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    void BeginThrown(const Vector3& initialVelocity) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ExecuteAbility(class Player* player) override;
    void UpdateCarriedAbility(class Player* player, float deltaTime) override;

private:
    // 放電攻撃、常時オーラ、投げ状態の当たり判定調整を分けて管理する
    bool UpdateInactiveState(float deltaTime);
    bool UpdateThrowRecoveryState(float deltaTime);
    void EnsureBaseScale();
    void UpdateWildTimers(float deltaTime);
    void UpdateWildBehavior(float deltaTime, Vector3& velocity);
    void UpdateCombatBehavior(float deltaTime, Vector3& velocity, const Vector3& direction, float distance);
    void UpdateWanderBehavior(float deltaTime, Vector3& velocity);
    void ApplyGroundMovementAndAnimation(float deltaTime, Vector3& velocity);
    void UpdateFacing(const Vector3& direction);
        // 放電攻撃前のチャージ状態へ移行します。
void StartCharge();
    void UpdateCharge(float deltaTime, const Vector3& direction);
        // チャージ完了後に雷範囲攻撃を発生させます。
void ReleaseShock(const Vector3& direction);
    void DispatchShockDamage(const Vector3& center, const Vector3& direction, float radius, float damage);
    void UpdateIdleSpark(float deltaTime);
    void InitializeAuraEffect();
        // 雷オーラの形状、色、表示状態を本体に同期します。
void UpdateAuraEffect(float deltaTime);
    void HideAuraEffect();
    void CalculateAuraShape(Vector3& center, float& horizontalDiameter, float& verticalDiameter) const;
    void EmitOuterThunderEffect(const char* presetName, int count, float phaseOffset = 0.0f);
    void EmitThunderPreset(const char* presetName, const Vector3& position);
    void ApplySlimeAnimation(float deltaTime);
    void SyncWorldCollisionRadius(float worldRadius);
    void SyncGroundCollisionRadius();
    void SyncThrownCollisionRadius();

    float attackCooldown_ = 1.15f;       // 野生時の放電再使用待ち。
    float chargeTimer_ = 0.0f;           // 放電前のタメ時間。
    float chargeParticleTimer_ = 0.0f;   // タメ中の火花発生間隔。
    float shockSquashTimer_ = 0.0f;      // 放電後の横広がり演出。
    float idleTimer_ = 0.0f;             // 待機アニメーション用。
    float groundHopTimer_ = 0.0f;        // 通常移動中の小ホップ周期。
    float idleSparkTimer_ = 0.0f;        // 常時火花の発生間隔。
    float carriedShockCooldown_ = 0.0f;  // 持ち運び能力の再使用待ち。
    float carriedEffectTimer_ = 0.0f;    // 持ち運び発動後の発光演出。
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 lastShockDirection_ = { 0.0f, 0.0f, 1.0f };
    std::unique_ptr<EffectObject3d> auraEffect_;
    bool hasBaseScale_ = false;
    bool isCharging_ = false;
};

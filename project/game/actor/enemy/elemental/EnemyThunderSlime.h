#pragma once
#include "BaseEnemy.h"
#include "EffectObject3d.h"
#include <array>
#include <memory>

// EnemyThunderSlimeは、チャージ放電、雷オーラ、待機火花を使う属性スライム敵です。
class EnemyThunderSlime : public BaseEnemy {
  public:
    /// 所有する雷オーラの表示を解放する。
    ~EnemyThunderSlime() override;
    // 雷スライム用モデル、Collider、雷演出の初期状態を準備します。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    // 徘徊、追跡、チャージ、放電、雷オーラを更新します。
    void Update(float deltaTime) override;
    // 本体と雷オーラをライト情報付きで描画します。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// 投げ状態の速度を設定し、雷攻撃を中断する。
    void BeginThrown(const engine::Vector3& initialVelocity) override;
    /// 配置用の雷スライム複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;
    // 攻撃候補を限定し、進行中のチャージと再使用待ちをリセットする。
    void SetDebugPreviewAttackId(const std::string& attackId);
    /// デバッグ表示向けに放電攻撃の段階名を返す。
    const char* GetDebugAttackPhaseName() const;
    /// 配置スケールを待機・放電演出の基準へ反映する。
    void ApplyManagedScale(const engine::Vector3& scale) override {
        baseScale_ = scale;
        hasBaseScale_ = true;
        SetScale(scale);
    }

  private:
    // 放電攻撃、常時オーラ、投げ状態の当たり判定調整を分けて管理する
    /// 行動不能中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateInactiveState(float deltaTime);
    /// 投げ復帰中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateThrowRecoveryState(float deltaTime);
    /// 未設定なら現在の配置スケールを記録する。
    void EnsureBaseScale();
    void UpdateWildTimers(float deltaTime);
    void UpdateWildBehavior(float deltaTime, engine::Vector3& velocity);
    void UpdateCombatBehavior(float deltaTime, engine::Vector3& velocity, const engine::Vector3& direction, float distance);
    /// 野生時の落雷攻撃に向けて狙い方向を固定する。
    void StartWildLightning(const engine::Vector3& direction);
    /// 落雷のチャージ、予告、連続着弾を進める。
    void UpdateWildLightning(float deltaTime);
    /// 指定地点に落雷の表示と判定を発生させる。
    void SpawnWildLightningStrike(const engine::Vector3& groundPosition);
    /// 落雷地点の範囲内へダメージを通知する。
    void DispatchWildLightningDamage(const engine::Vector3& groundPosition);
    /// 野生時落雷の進行状態を初期状態へ戻す。
    void ResetWildLightning();
    void UpdateWanderBehavior(float deltaTime, engine::Vector3& velocity);
    void ApplyGroundMovementAndAnimation(float deltaTime, engine::Vector3& velocity);
    void UpdateFacing(const engine::Vector3& direction);
    // 放電攻撃前のチャージ状態へ移行します。
    void StartCharge();
    void UpdateCharge(float deltaTime, const engine::Vector3& direction);
    // チャージ完了後に雷範囲攻撃を発生させます。
    void ReleaseShock(const engine::Vector3& direction);
    void DispatchShockDamage(const engine::Vector3& center, const engine::Vector3& direction, float radius, float damage);
    /// 待機中に発生する小さな火花を更新する。
    void UpdateIdleSpark(float deltaTime);
    /// 雷オーラの描画対象を作成する。
    void InitializeAuraEffect();
    // 雷オーラの形状、色、表示状態を本体に同期します。
    void UpdateAuraEffect(float deltaTime);
    /// 雷オーラを非表示にする。
    void HideAuraEffect();
    /// 体の大きさから雷オーラの中心と縦横の径を計算する。
    void CalculateAuraShape(engine::Vector3& center, float& horizontalDiameter, float& verticalDiameter) const;
    void EmitOuterThunderEffect(const char* presetName, int count, float phaseOffset = 0.0f);
    void EmitLineChargeEffect(const char* presetName, float progress);
    void EmitThunderPreset(const char* presetName, const engine::Vector3& position);
    engine::Vector3 FindStrikeGround(const engine::Vector3& samplePosition, engine::Object3d* ignoredObject, float fallbackHeight) const;
    void SpawnLightningStrikeVisual(const engine::Vector3& groundPosition, const engine::Vector3& direction);
    void SpawnChargeGroundEffect(const engine::Vector3& groundPosition, float scale = 1.0f);
    void ApplySlimeAnimation(float deltaTime);
    void SyncWorldCollisionRadius(float worldRadius);
    void SyncGroundCollisionRadius();
    void SyncThrownCollisionRadius();

    float attackCooldown_ = 1.15f;     // 野生時の放電再使用待ち。
    float chargeTimer_ = 0.0f;         // 放電前のタメ時間。
    float chargeParticleTimer_ = 0.0f; // タメ中の火花発生間隔。
    float shockSquashTimer_ = 0.0f;    // 放電後の横広がり演出。
    float idleTimer_ = 0.0f;           // 待機アニメーション用。
    float groundHopTimer_ = 0.0f;      // 通常移動中の小ホップ周期。
    float idleSparkTimer_ = 0.0f;      // 常時火花の発生間隔。
    enum class WildLightningState {
        Idle,
        Charging,
        Striking,
    };
    WildLightningState wildLightningState_ = WildLightningState::Idle;
    std::array<engine::Vector3, 5> wildStrikePositions_{};
    engine::Vector3 wildLightningDirection_ = {0.0f, 0.0f, 1.0f};
    float wildLightningTimer_ = 0.0f;
    float wildLightningEffectTimer_ = 0.0f;
    int wildStrikeIndex_ = 0;
    bool wildLightningHitTarget_ = false;
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 lastShockDirection_ = {0.0f, 0.0f, 1.0f};
    std::unique_ptr<EffectObject3d> auraEffect_;
    bool hasBaseScale_ = false;
    bool isCharging_ = false;
    bool chargeWarningTriggered_ = false;
    std::string debugPreviewAttackId_;
};

#pragma once

#include "game/actor/enemy/combat/AttackTelegraph.h"
#include "BaseGimmick.h"

#include <memory>
#include <array>

// 警告、噴出、休止を周期的に繰り返すマグマ噴出口です。
class GimmickMagmaGeyser : public BaseGimmick {
  public:
    /// 噴出口のモデル、周期、攻撃予告を初期化する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 警告、噴出、休止の周期を進める。
    void Update(float deltaTime) override;
    /// 噴出口本体と通常材質の演出を描画する。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// 指定カメラのプレビューへ噴出口を描画する。
    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource,
                       int previewBufferIndex = 0) override;
    /// 接触対象への攻撃判定を処理する。
    bool OnCollision(engine::Object3d* other) override;
    /// 外部トリガーによる噴出を要求する。
    void OnTrigger() override;
    /// スイッチ状態に応じて周期動作を有効・無効にする。
    void OnSwitchEvent(bool active) override;

    /// 特殊材質で描く噴出表示を所有するか返す。
    bool HasOwnedSpecialMaterialVisuals() const override;
    /// 噴出表示を通常カメラの特殊材質パスへ描く。
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;
    /// 噴出表示をプレビューカメラの特殊材質パスへ描く。
    void DrawOwnedSpecialMaterialVisualsForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                                  int previewBufferIndex = 0) override;

    /// 配置用の噴出口複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;

    // 移動床は同じ周期から上昇量を取得し、独立したタイマーを持たない。
    // ワールド距離の上昇量と、0～1 に収まる予兆/圧力の進行率を同一周期から返します。
    float GetLiftOffset() const;
    float GetWarningProgress() const;
    float GetPressureEnvelope() const;

    /// 逃走イベント向けに噴出周期と有効状態を初期値へ戻す。
    void ResetEscapeCycle(bool active);
    // Timeline の信号で単発噴出へ切り替えます。編集中または処理中の重複要求は無視します。
    void RequestFireballEruption();

  private:
    /// 単発噴出の火球を生成する。
    void FireMagmaProjectiles();
    /// 単発噴出の予告から火球発射までを進める。
    void UpdateSingleEruption(float deltaTime);
    bool singleEruption_ = false;
    bool singleFired_ = false;
    float singleTimer_ = 0.0f;
    /// 単発噴出で飛ぶ火球の移動時間を返す。
    float GetFireballFlightDuration() const;

    enum class Phase {
        Warning,
        Erupting,
        Cooldown,
    };

    /// Play 中の更新か判定する。
    bool IsPlaying() const;
    /// プレイヤーが噴出口のシミュレーション範囲内か返す。
    bool IsPlayerWithinSimulationRange() const;
    /// Play 開始時の周期と演出を設定する。
    void BeginPlayCycle();
    /// Play 外の配置表示へ戻す。
    void ResetOutsidePlay();
    /// 攻撃予告段階へ切り替える。
    void BeginWarning();
    /// 噴出段階へ切り替え、当たり判定を有効にする。
    void BeginEruption();
    /// 指定時間の休止段階へ切り替える。
    void BeginCooldown(float duration);
    /// 現在段階の終了後に次の段階へ進める。
    void AdvancePhase();
    /// 警告表示の進行率を噴出周期へ同期する。
    void UpdateWarningVisual();
    /// 噴出柱の形状と表示を更新する。
    void UpdateEruptionVisual(float deltaTime);
    /// 休止中の見た目を更新する。
    void UpdateCooldownVisual();
    /// 圧力に応じて煮え立つ泡を更新する。
    void UpdateBoilingVisual(float deltaTime, float pressure, bool visible);
    /// 泡の表示をすべて隠す。
    void HideBoilingVisuals();
    /// 噴出高さに応じて当たり判定を有効・無効化する。
    void ApplyCollisionState(bool enabled, float activeHeight);
    /// 噴出開始時の一回限りの演出を発生させる。
    void SpawnEruptionBurst();
    /// 噴出中の粒子を発生させる。
    void EmitEruptionParticles();

    engine::Vector3 GetMouthPosition() const;
    float GetWarningDuration() const;
    float GetEruptionDuration() const;
    float GetCooldownDuration() const;
    float GetInitialDelay() const;
    float GetDamage() const;
    float GetGeyserHeight() const;
    float GetGeyserRadius() const;
    float GetHorizontalKnockback() const;
    float GetVerticalKnockback() const;
    float GetSimulationWakeDistance() const;

    AttackTelegraph warningTelegraph_;
    std::unique_ptr<engine::Object3d> eruptionVisual_;
    // 泡は使い回し、予告中に生成・破棄を繰り返さない。
    std::array<std::unique_ptr<engine::Object3d>, 4> boilingVisuals_;
    float boilingClock_ = 0.0f;
    bool ventInLiquid_ = false;
    Phase phase_ = Phase::Warning;
    float phaseTimer_ = 0.0f;
    float phaseDuration_ = 1.0f;
    float damageCooldownTimer_ = 0.0f;
    float particleTimer_ = 0.0f;
    float visualTimer_ = 0.0f;
    int particleBandIndex_ = 0;
    bool initializedForPlay_ = false;
    bool active_ = true;
    bool simulationSleeping_ = false;
    engine::Vector4 baseColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
    float baseEmissive_ = 1.0f;
};

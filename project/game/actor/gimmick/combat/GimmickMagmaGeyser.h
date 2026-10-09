#pragma once

#include "game/actor/enemy/combat/AttackTelegraph.h"
#include "BaseGimmick.h"

#include <memory>
#include <array>

// 警告、噴出、休止を周期的に繰り返すマグマ噴出口です。
class GimmickMagmaGeyser : public BaseGimmick {
  public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource,
                       int previewBufferIndex = 0) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    bool HasOwnedSpecialMaterialVisuals() const override;
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;
    void DrawOwnedSpecialMaterialVisualsForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                                  int previewBufferIndex = 0) override;

    std::unique_ptr<Object3d> Clone() const override;

    // 移動床は同じ周期から上昇量を取得し、独立したタイマーを持たない。
    // ワールド距離の上昇量と、0～1 に収まる予兆/圧力の進行率を同一周期から返します。
    float GetLiftOffset() const;
    float GetWarningProgress() const;
    float GetPressureEnvelope() const;

    void ResetEscapeCycle(bool active);
    // Timeline の信号で単発噴出へ切り替えます。編集中または処理中の重複要求は無視します。
    void RequestFireballEruption();

  private:
    void FireMagmaProjectiles();
    void UpdateSingleEruption(float deltaTime);
    bool singleEruption_ = false;
    bool singleFired_ = false;
    float singleTimer_ = 0.0f;
    float GetFireballFlightDuration() const;

    enum class Phase {
        Warning,
        Erupting,
        Cooldown,
    };

    bool IsPlaying() const;
    bool IsPlayerWithinSimulationRange() const;
    void BeginPlayCycle();
    void ResetOutsidePlay();
    void BeginWarning();
    void BeginEruption();
    void BeginCooldown(float duration);
    void AdvancePhase();
    void UpdateWarningVisual();
    void UpdateEruptionVisual(float deltaTime);
    void UpdateCooldownVisual();
    void UpdateBoilingVisual(float deltaTime, float pressure, bool visible);
    void HideBoilingVisuals();
    void ApplyCollisionState(bool enabled, float activeHeight);
    void SpawnEruptionBurst();
    void EmitEruptionParticles();

    Vector3 GetMouthPosition() const;
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
    std::unique_ptr<Object3d> eruptionVisual_;
    // 泡は使い回し、予告中に生成・破棄を繰り返さない。
    std::array<std::unique_ptr<Object3d>, 4> boilingVisuals_;
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
    Vector4 baseColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
    float baseEmissive_ = 1.0f;
};

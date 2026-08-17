#pragma once

#include "AttackTelegraph.h"
#include "BaseGimmick.h"

#include <memory>

// 警告、噴出、休止を周期的に繰り返すマグマ噴出口です。
class GimmickMagmaGeyser : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    bool HasOwnedSpecialMaterialVisuals() const override;
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
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
    Vector4 baseColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float baseEmissive_ = 1.0f;
};

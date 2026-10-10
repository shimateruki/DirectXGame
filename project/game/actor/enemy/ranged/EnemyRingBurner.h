#pragma once

#include "BaseEnemy.h"
#include <cstdint>

// 近づくと地面すれすれの熱輪を全方向へ放つ固定砲台です。
// 天板は足場として使え、上に乗っている間は発射を停止します。
class EnemyRingBurner : public BaseEnemy {
  public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void SetCarried(bool isCarried) override;
    bool IsPullImmune() const override { return true; }
    void ApplyManagedScale(const Vector3& scale) override;

  protected:
    void CaptureReplayCustomState(json& state) const override;
    void RestoreReplayCustomState(const json& state) override;

  private:
    // 数値は Replay JSON に保存されるため変更しない。
    enum class State { Idle = 0, Charge = 1, Wave = 2, Recovery = 3, Suppressed = 4 };

    class StateBehavior;
    class IdleBehavior;
    class ChargeBehavior;
    class WaveBehavior;
    class RecoveryBehavior;
    class SuppressedBehavior;
    static const StateBehavior& BehaviorFor(State state);

    bool UpdateInactiveState(float deltaTime);
    void ResolveRuntimeTarget();
    void EnsureBaseScale();
    void EnterState(State state, float duration);
    void UpdateCombat(float deltaTime);
    void UpdateWaveDamage(float previousRadius);
    void StopWaveEffects();
    void UpdateVisualPose(float deltaTime);
    bool IsTargetStandingOnTop() const;
    float GetPlanarTargetDistance() const;
    float GetChargeDuration() const;
    float GetCooldownDuration() const;
    float GetWaveMaximumRadius() const;
    void SpawnChargeEffects();
    void SpawnWaveEffects();

    State state_ = State::Idle;
    Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    float stateTimer_ = 0.0f;
    float stateDuration_ = 0.0f;
    float cooldownTimer_ = 0.65f;
    float waveRadius_ = 0.0f;
    float idleTimer_ = 0.0f;
    float particleTimer_ = 0.0f;
    bool hasBaseScale_ = false;
    bool waveDamageDone_ = false;
    uint64_t waveEffectScope_ = 0;
};

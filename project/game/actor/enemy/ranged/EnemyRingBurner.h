#pragma once

#include "BaseEnemy.h"

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
    enum class State {
        Idle,
        Charge,
        Wave,
        Recovery,
        Suppressed
    };

    bool UpdateInactiveState(float deltaTime);
    void ResolveRuntimeTarget();
    void EnsureBaseScale();
    void EnterState(State state, float duration);
    void BeginCharge();
    void BeginWave();
    void UpdateCombat(float deltaTime);
    void UpdateWaveDamage();
    void UpdateVisualPose(float deltaTime);
    bool IsTargetStandingOnTop() const;
    float GetPlanarTargetDistance() const;
    float GetChargeDuration() const;
    float GetCooldownDuration() const;
    float GetWaveMaximumRadius() const;
    void SpawnChargeEffects();
    void SpawnWaveEffects();

    State state_ = State::Idle;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    float stateTimer_ = 0.0f;
    float stateDuration_ = 0.0f;
    float cooldownTimer_ = 0.65f;
    float waveRadius_ = 0.0f;
    float idleTimer_ = 0.0f;
    float particleTimer_ = 0.0f;
    bool hasBaseScale_ = false;
    bool waveDamageDone_ = false;
};

#pragma once
#include "BaseEnemy.h"

class EnemyGiantSlime : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void BeginThrown(const Vector3& initialVelocity) override;
    std::unique_ptr<Object3d> Clone() const override;
    void BeginHookSplitPull(const Vector3& hookOwnerPos);
    bool UpdateHookSplitPull(float deltaTime, const Vector3& hookOwnerPos, class ParticleSystem* particleSystem);
    void CancelHookSplitPull();
    float GetHookSplitProgress() const;
    bool HasSplit() const { return hasSplit_; }

private:
    enum class State {
        Idle,
        ChargeJump,
        Airborne,
        Recovery
    };

    bool UpdateInactiveState(float deltaTime);
    bool UpdateHookSplitState(float deltaTime);
    bool UpdateThrowRecoveryState(float deltaTime);
    void EnsureBaseScale();
    void UpdateTimers(float deltaTime);
    void UpdateTargetFacing(Vector3& direction, float& distance);
    void UpdateStateMachine(float deltaTime, const Vector3& direction, float distance);
    void UpdateIdleState(float deltaTime, const Vector3& direction, float distance);
    void LaunchJump(const Vector3& direction, float distance);
    void BeginJumpCharge(const Vector3& direction, float distance);
    void UpdateJumpCharge(float deltaTime);
    void BeginLandingRecovery();
    void UpdateLandingRecovery(float deltaTime);
    void SpawnLandingTelegraph();
    void SpawnLandingEffects();
    void DispatchLandingShockwave();
    void ApplySlimeAnimation(float deltaTime);
    void SyncWorldCollisionRadius(float worldRadius, float worldCenterY);
    void SyncGroundCollisionRadius();
    void SyncThrownCollisionRadius();
    void SplitIntoSmallSlimes(class ParticleSystem* particleSystem);

    float jumpTimer_ = 0.0f;
    float landingPulseTimer_ = 0.0f;
    float idleTimer_ = 0.0f;
    float hookSplitPullTimer_ = 0.0f;
    bool isJumpingAttack_ = false;
    bool isHookSplitPulled_ = false;
    bool hasSplit_ = false;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 hookSplitBasePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookSplitBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 landingTelegraphPosition_ = { 0.0f, 0.0f, 0.0f };
    State state_ = State::Idle;
    float stateTimer_ = 0.0f;
    float telegraphTimer_ = 0.0f;
    bool hookSplitWeakPoint_ = false;
    bool hasBaseScale_ = false;
};

#pragma once
#include "BaseEnemy.h"

// EnemyGiantSlimeは、大型ジャンプ攻撃、着地衝撃波、フック分裂を持つ重量級スライム敵です。
class EnemyGiantSlime : public BaseEnemy {
public:
        // 巨大スライム用モデル、Collider、基準スケール、ステートを初期化します。
void Initialize(Object3dCommon* common, const std::string& modelName) override;
        // ステートマシン、ジャンプ攻撃、着地回復、分裂処理を更新します。
void Update(float deltaTime) override;
    void BeginThrown(const Vector3& initialVelocity) override;
    std::unique_ptr<Object3d> Clone() const override;
        // フックで分裂させるための引っ張り状態を開始します。
void BeginHookSplitPull(const Vector3& hookOwnerPos);
        // フック引っ張り進行を更新し、完了時に小スライムへ分裂します。
bool UpdateHookSplitPull(float deltaTime, const Vector3& hookOwnerPos, class ParticleSystem* particleSystem);
    void CancelHookSplitPull();
    float GetHookSplitProgress() const;
    const char* GetDebugHookSplitPhaseName() const;
    Vector3 GetHookAttachmentPosition() const;
    bool IsHookSplitPulled() const { return isHookSplitPulled_; }
    bool IsHookSplitAwaitingLanding() const { return hookSplitAwaitingLanding_; }
    bool HasSplit() const { return hasSplit_; }
    void ApplyManagedScale(const Vector3& scale) override {
        baseScale_ = scale;
        hasBaseScale_ = true;
        SetScale(scale);
    }

private:
        // 巨大スライムの待機、チャージ、ジャンプ、着地回復などの状態です。
enum class State {
        Idle,
        ChargeJump,
        Airborne,
        Recovery
    };

    // フック拘束中の落下、着地、踏ん張り、破断を分離した見た目用ステートです。
    enum class HookSplitPhase {
        None,
        AirborneTether,
        LandingSquash,
        Brace,
        Slip,
        Tear
    };

    bool UpdateInactiveState(float deltaTime);
    bool UpdateHookSplitState(float deltaTime);
    bool UpdateThrowRecoveryState(float deltaTime);
    void EnsureBaseScale();
    void UpdateTimers(float deltaTime);
    void UpdateTargetFacing(Vector3& direction, float& distance);
    void UpdateStateMachine(float deltaTime, const Vector3& direction, float distance);
    void UpdateIdleState(float deltaTime, const Vector3& direction, float distance);
        // 目標方向と距離に応じてジャンプ攻撃を開始します。
void LaunchJump(const Vector3& direction, float distance);
    void BeginJumpCharge(const Vector3& direction, float distance);
    void UpdateJumpCharge(float deltaTime);
    void BeginLandingRecovery();
    void UpdateLandingRecovery(float deltaTime);
    void SpawnLandingTelegraph();
        // 着地時のパーティクルや画面演出を発生させます。
void SpawnLandingEffects();
    void DispatchLandingShockwave();
    void ApplySlimeAnimation(float deltaTime);
    void BeginGroundHookResistance(bool landedFromAir);
    void ApplyHookSplitVisual(float deltaTime, const Vector3& hookOwnerPos);
    Vector3 CalculateHookAnchorOffset(const Vector3& visualScale, const Vector3& visualRotation) const;
    void ResetHookSplitVisual();
    void SyncWorldCollisionRadius(float worldRadius, float worldCenterY);
    void SyncGroundCollisionRadius();
    void SyncThrownCollisionRadius();
        // フック分裂完了時に小型スライムを生成します。
void SplitIntoSmallSlimes(class ParticleSystem* particleSystem);

    float jumpTimer_ = 0.0f;
    float landingPulseTimer_ = 0.0f;
    float idleTimer_ = 0.0f;
    float hookSplitPullTimer_ = 0.0f;
    float hookSplitPhaseTimer_ = 0.0f;
    bool isJumpingAttack_ = false;
    bool isHookSplitPulled_ = false;
    bool hasSplit_ = false;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 hookSplitBasePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookSplitBaseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 hookSplitOwnerPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookSplitDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector3 hookVisualScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 hookVisualScaleVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookVisualRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookVisualRotationVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookVisualOffset_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookVisualOffsetVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookVisualShear_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookVisualShearVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 landingTelegraphPosition_ = { 0.0f, 0.0f, 0.0f };
    State state_ = State::Idle;
    HookSplitPhase hookSplitPhase_ = HookSplitPhase::None;
    float stateTimer_ = 0.0f;
    float telegraphTimer_ = 0.0f;
    bool hookSplitWeakPoint_ = false;
    bool hookSplitAwaitingLanding_ = false;
    bool hasBaseScale_ = false;
    bool landingWarningTriggered_ = false;
};

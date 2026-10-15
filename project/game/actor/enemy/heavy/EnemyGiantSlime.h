#pragma once
#include "BaseEnemy.h"

// EnemyGiantSlimeは、大型ジャンプ攻撃、着地衝撃波、フック分裂を持つ重量級スライム敵です。
class EnemyGiantSlime : public BaseEnemy {
  public:
    // 巨大スライム用モデル、Collider、基準スケール、ステートを初期化します。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    // ステートマシン、ジャンプ攻撃、着地回復、分裂処理を更新します。
    void Update(float deltaTime) override;
    /// 投げられた速度を設定して攻撃状態を中断する。
    void BeginThrown(const engine::Vector3& initialVelocity) override;
    /// 配置用の巨大スライム複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;
    // フックで分裂させるための引っ張り状態を開始します。
    void BeginHookSplitPull(const engine::Vector3& hookOwnerPos);
    // フック引っ張り進行を更新し、完了時に小スライムへ分裂します。
    bool UpdateHookSplitPull(float deltaTime, const engine::Vector3& hookOwnerPos, class ParticleSystem* particleSystem);
    /// フック拘束と分裂の進行を中断する。
    void CancelHookSplitPull();
    /// フック分裂の進行率を返す。
    float GetHookSplitProgress() const;
    /// デバッグ表示向けにフック分裂の段階名を返す。
    const char* GetDebugHookSplitPhaseName() const;
    /// フックが接続するワールド座標を返す。
    engine::Vector3 GetHookAttachmentPosition() const;
    bool IsHookSplitPulled() const { return isHookSplitPulled_; }
    bool IsHookSplitAwaitingLanding() const { return hookSplitAwaitingLanding_; }
    bool HasSplit() const { return hasSplit_; }
    /// 配置スケールをジャンプと分裂演出の基準にする。
    void ApplyManagedScale(const engine::Vector3& scale) override {
        baseScale_ = scale;
        hasBaseScale_ = true;
        SetScale(scale);
    }

  private:
    /// 本体との接触ダメージを返す。
    float GetBodyAttackDamage() const override;
    // 巨大スライムの待機、チャージ、ジャンプ、着地回復などの状態です。
    enum class State { Idle, ChargeJump, Airborne, Recovery };

    // フック拘束中の落下、着地、踏ん張り、破断を分離した見た目用ステートです。
    enum class HookSplitPhase { None, AirborneTether, LandingSquash, Brace, Slip, Tear };

    /// 行動不能中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateInactiveState(float deltaTime);
    /// フック分裂中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateHookSplitState(float deltaTime);
    /// 投げ復帰中の更新を処理し、通常行動を止める場合は true を返す。
    bool UpdateThrowRecoveryState(float deltaTime);
    /// 未設定なら現在の配置スケールを記録する。
    void EnsureBaseScale();
    void UpdateTimers(float deltaTime);
    void UpdateTargetFacing(engine::Vector3& direction, float& distance);
    void UpdateStateMachine(float deltaTime, const engine::Vector3& direction, float distance);
    void UpdateIdleState(float deltaTime, const engine::Vector3& direction, float distance);
    // 目標方向と距離に応じてジャンプ攻撃を開始します。
    void LaunchJump(const engine::Vector3& direction, float distance);
    void BeginJumpCharge(const engine::Vector3& direction, float distance);
    void UpdateJumpCharge(float deltaTime);
    void BeginLandingRecovery();
    void UpdateLandingRecovery(float deltaTime);
    void SpawnLandingTelegraph();
    // 着地時のパーティクルや画面演出を発生させます。
    void SpawnLandingEffects();
    /// 着地の衝撃波に入った対象へダメージを通知する。
    void DispatchLandingShockwave();
    void ApplySlimeAnimation(float deltaTime);
    /// フックで引かれる際の踏ん張り演出へ移る。
    void BeginGroundHookResistance(bool landedFromAir);
    /// フック所有者の位置に合わせて引っ張られた見た目を更新する。
    void ApplyHookSplitVisual(float deltaTime, const engine::Vector3& hookOwnerPos);
    /// 変形した体に対するフック接続位置の補正を返す。
    engine::Vector3 CalculateHookAnchorOffset(const engine::Vector3& visualScale, const engine::Vector3& visualRotation) const;
    /// フック分裂用の変形を戻す。
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
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 hookSplitBasePosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hookSplitBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 hookSplitOwnerPosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hookSplitDirection_ = {0.0f, 0.0f, 1.0f};
    engine::Vector3 hookVisualScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 hookVisualScaleVelocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hookVisualRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hookVisualRotationVelocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hookVisualOffset_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hookVisualOffsetVelocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hookVisualShear_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hookVisualShearVelocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 landingTelegraphPosition_ = {0.0f, 0.0f, 0.0f};
    State state_ = State::Idle;
    HookSplitPhase hookSplitPhase_ = HookSplitPhase::None;
    float stateTimer_ = 0.0f;
    float telegraphTimer_ = 0.0f;
    bool hookSplitWeakPoint_ = false;
    bool hookSplitAwaitingLanding_ = false;
    bool hasBaseScale_ = false;
    bool landingWarningTriggered_ = false;
};

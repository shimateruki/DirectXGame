#pragma once
#include "BaseEnemy.h"

// EnemyGiantSlimeは、大型ジャンプ攻撃、着地衝撃波、フック分裂を持つ重量級スライム敵です。
// 関連：BaseEnemyの移動と被ダメージ処理を使い、突進・フック時の分裂を制御する。
// 主な入口：Initialize / Update
class EnemyGiantSlime : public BaseEnemy {
  public:
    // 巨大スライム用モデル、Collider、基準スケール、ステートを初期化します。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    // ステートマシン、ジャンプ攻撃、着地回復、分裂処理を更新します。
    void Update(float deltaTime) override;

    /// 投げられた速度を設定して攻撃状態を中断する。
    void BeginThrown(const engine::Vector3& initialVelocity) override;

    /// 配置用の巨大スライム複製を作る。
    std::unique_ptr<engine::Object3D> Clone() const override;

    // フックで分裂させるための引っ張り状態を開始します。
    void BeginHookSplitPull(const engine::Vector3& hookOwnerPos);

    // フック引っ張り進行を更新し、完了時に小スライムへ分裂します。
    bool UpdateHookSplitPull(float deltaTime, const engine::Vector3& hookOwnerPos, engine::ParticleSystem* particleSystem);

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

    // 着地の発光は残り時間だけ減らし、負の値を後続の演出へ渡さない。
    void UpdateTimers(float deltaTime);

    // 高低差は照準に含めず、交戦前は索敵範囲に入った対象だけへ向きを変える。
    void UpdateTargetFacing(engine::Vector3& direction, float& distance);

    // 滞空中は着地予測地点を表示し、落下時間に合わせて警告を一度だけ強める。
    void UpdateStateMachine(float deltaTime, const engine::Vector3& direction, float distance);

    void UpdateIdleState(float deltaTime, const engine::Vector3& direction, float distance);

    // 目標方向と距離に応じてジャンプ攻撃を開始します。
    void LaunchJump(const engine::Vector3& direction, float distance);

    // 射程に収めた着地点を予告し、溜め中の移動を止めて回避の猶予を作る。
    void BeginJumpCharge(const engine::Vector3& direction, float distance);

    // 溜め中は対象の移動を追って着地点を更新し、発射後は予告地点を固定する。
    void UpdateJumpCharge(float deltaTime);

    // 着地の瞬間だけ衝撃波とエフェクトを発生させ、回復状態へ移す。
    void BeginLandingRecovery();

    // 潰れた姿を回復時間に沿って元へ戻し、終了時に色とサイズを復元する。
    void UpdateLandingRecovery(float deltaTime);

    // 予告リングを地面より少し上に置き、地形との重なりで消えないようにする。
    void SpawnLandingTelegraph();

    // 着地時のパーティクルや画面演出を発生させます。
    void SpawnLandingEffects();

    /// 着地の衝撃波に入った対象へダメージを通知する。
    void DispatchLandingShockwave();

    // 攻撃段階ごとの潰れ・伸びと被弾反応を合成し、配置時の大きさを基準に描画する。
    void ApplySlimeAnimation(float deltaTime);

    /// フックで引かれる際の踏ん張り演出へ移る。
    void BeginGroundHookResistance(bool landedFromAir);

    /// フック所有者の位置に合わせて引っ張られた見た目を更新する。
    void ApplyHookSplitVisual(float deltaTime, const engine::Vector3& hookOwnerPos);

    /// 変形した体に対するフック接続位置の補正を返す。
    engine::Vector3 CalculateHookAnchorOffset(const engine::Vector3& visualScale, const engine::Vector3& visualRotation) const;

    /// フック分裂用の変形を戻す。
    void ResetHookSplitVisual();

    // 表示スケールが変化しても判定のワールド半径が一定になるよう逆算する。
    void SyncWorldCollisionRadius(float worldRadius, float worldCenterY);

    // 接地中はプレイヤーと接触する通常の胴体範囲にする。
    void SyncGroundCollisionRadius();

    // 投げられた胴体は横方向へ広がるため、専用の半径と中心を使う。
    void SyncThrownCollisionRadius();

    // フック分裂完了時に小型スライムを生成します。
    void SplitIntoSmallSlimes(engine::ParticleSystem* particleSystem);

    float jumpTimer_ = 0.0f; // 跳躍の段階切替に使う経過秒数。
    float landingPulseTimer_ = 0.0f; // 着地の変形までの残り秒数。
    float idleTimer_ = 0.0f; // 待機中の見た目に使う経過秒数。

    float hookSplitPullTimer_ = 0.0f;
    float hookSplitPhaseTimer_ = 0.0f;

    bool isJumpingAttack_ = false;
    bool isHookSplitPulled_ = false;
    bool hasSplit_ = false;

    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};

    engine::Vector3 hookSplitBasePosition_ = engine::Vector3::ZERO;
    engine::Vector3 hookSplitBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 hookSplitOwnerPosition_ = engine::Vector3::ZERO;
    engine::Vector3 hookSplitDirection_ = {0.0f, 0.0f, 1.0f};

    engine::Vector3 hookVisualScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 hookVisualScaleVelocity_ = engine::Vector3::ZERO;

    engine::Vector3 hookVisualRotation_ = engine::Vector3::ZERO;
    engine::Vector3 hookVisualRotationVelocity_ = engine::Vector3::ZERO;

    engine::Vector3 hookVisualOffset_ = engine::Vector3::ZERO;
    engine::Vector3 hookVisualOffsetVelocity_ = engine::Vector3::ZERO;

    engine::Vector3 hookVisualShear_ = engine::Vector3::ZERO;
    engine::Vector3 hookVisualShearVelocity_ = engine::Vector3::ZERO;

    engine::Vector3 landingTelegraphPosition_ = engine::Vector3::ZERO;

    State state_ = State::Idle;
    HookSplitPhase hookSplitPhase_ = HookSplitPhase::None;
    float stateTimer_ = 0.0f; // 現在の状態に使う経過秒数。
    float telegraphTimer_ = 0.0f;

    bool hookSplitWeakPoint_ = false;
    bool hookSplitAwaitingLanding_ = false;

    bool hasBaseScale_ = false;
    bool landingWarningTriggered_ = false;
};

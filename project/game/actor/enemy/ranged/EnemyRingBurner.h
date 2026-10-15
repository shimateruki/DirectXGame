#pragma once

#include "BaseEnemy.h"
#include <cstdint>

// 近づくと地面すれすれの熱輪を全方向へ放つ固定砲台です。
// 天板は足場として使え、上に乗っている間は発射を停止します。
class EnemyRingBurner : public BaseEnemy {
  public:
    /// 固定砲台の当たり判定、攻撃半径、初期状態を設定する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 上に乗られた状態と熱輪の攻撃段階を進める。
    void Update(float deltaTime) override;
    /// 配置用の熱輪砲台複製を作る。
    std::unique_ptr<engine::Object3d> Clone() const override;
    /// 持ち上げ状態を基底側へ通知する。
    void SetCarried(bool isCarried) override;
    bool IsPullImmune() const override { return true; }
    /// 配置スケールを攻撃演出の基準へ反映する。
    void ApplyManagedScale(const engine::Vector3& scale) override;

  protected:
    /// 熱輪の攻撃段階と半径を Replay JSON に記録する。
    void CaptureReplayCustomState(json& state) const override;
    /// Replay JSON から熱輪の攻撃段階と半径を復元する。
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
    /// 攻撃状態に対応する振る舞い実装を返す。
    static const StateBehavior& BehaviorFor(State state);

    /// 行動不能中の状態を処理し、攻撃更新を止める場合は true を返す。
    bool UpdateInactiveState(float deltaTime);
    /// シーン上の攻撃対象を解決する。
    void ResolveRuntimeTarget();
    /// 未設定なら現在の配置スケールを記録する。
    void EnsureBaseScale();
    /// 攻撃状態と継続時間を切り替える。
    void EnterState(State state, float duration);
    /// 予備動作、熱輪拡大、回復状態を進める。
    void UpdateCombat(float deltaTime);
    /// 前回半径から新たに通過した範囲へのダメージを判定する。
    void UpdateWaveDamage(float previousRadius);
    /// 熱輪に属する演出の発生を止める。
    void StopWaveEffects();
    /// 攻撃状態に応じて砲台の姿勢を更新する。
    void UpdateVisualPose(float deltaTime);
    /// 対象が天板の上に立っているか判定する。
    bool IsTargetStandingOnTop() const;
    /// 対象までの水平距離を返す。
    float GetPlanarTargetDistance() const;
    /// 予備動作の長さを攻撃定義から返す。
    float GetChargeDuration() const;
    /// 次の攻撃までの待ち時間を返す。
    float GetCooldownDuration() const;
    /// 熱輪の到達半径を返す。
    float GetWaveMaximumRadius() const;
    /// 予備動作中の演出を発生させる。
    void SpawnChargeEffects();
    /// 熱輪の拡大に合わせた演出を発生させる。
    void SpawnWaveEffects();

    State state_ = State::Idle;
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
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

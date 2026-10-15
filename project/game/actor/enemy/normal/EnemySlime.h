#pragma once
#include "BaseEnemy.h"

// 基礎変身の見本になる、溜めジャンプから急降下を行うピンクスライム。
class EnemySlime : public BaseEnemy {
  public:
    /// 基本スライムのモデル、当たり判定、攻撃状態を初期化する。
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// 徘徊から急降下、着地回復までの行動を進める。
    void Update(float deltaTime) override;
    /// 現在の配置設定を引き継ぐ基本スライムを生成する。
    std::unique_ptr<engine::Object3d> Clone() const override;
    /// 配置側で指定した大きさを攻撃演出の基準として反映する。
    void ApplyManagedScale(const engine::Vector3& scale) override;
    /// デバッグ表示向けに現在の攻撃段階名を返す。
    const char* GetDebugMoveStateName() const;

  private:
    /// 本体との接触で与えるダメージを返す。
    float GetBodyAttackDamage() const override;
    // 徘徊から溜め、上昇、急降下、着地へ進む攻撃状態です。
    enum class MoveState {
        Wander,
        Charge,
        Rise,
        Dive,
        Recover,
    };

    // 移動・攻撃の切り替えと、対象への平面方向を計算します。
    /// 未設定なら現在の大きさをアニメーションの基準として記録する。
    void EnsureBaseScale();
    /// 対象への水平移動方向を返し、必要なら距離も書き出す。
    engine::Vector3 GetTargetPlanarDirection(float* outDistance = nullptr) const;
    /// 現在向いている水平方向を返す。
    engine::Vector3 GetFacingPlanarDirection() const;
    /// 指定方向へ turnRate に応じて向きを変える。
    void FaceDirection(const engine::Vector3& direction, float turnRate);
    /// 徘徊と次のジャンプ開始条件を評価する。
    void UpdateWander(float deltaTime);
    /// 溜め状態の時間と対象方向を更新する。
    void UpdateCharge(float deltaTime, const engine::Vector3& direction, float distance);
    /// 上昇に使う進行方向と初期速度を設定する。
    void BeginRise(const engine::Vector3& direction);
    /// ジャンプの上昇段階を進める。
    void UpdateRise(float deltaTime);
    /// 対象方向へ急降下する段階を開始する。
    void BeginDive(const engine::Vector3& direction, float distance);
    /// 急降下中の移動と着地判定を進める。
    void UpdateDive(float deltaTime);
    /// 着地後の硬直と見た目の回復を開始する。
    void BeginLandingRecovery();
    /// 硬直終了まで着地演出を更新する。
    void UpdateLandingRecovery(float deltaTime);
    // 予備動作、飛行、着地の演出は判定処理と独立して発生させます。
    void SpawnChargePulseEffect(float deltaTime, float chargeRate);
    void SpawnChargeDebrisEffect(float deltaTime, float chargeRate);
    void SpawnChargeVortexEffect(float deltaTime, float chargeRate);
    void SpawnLaunchEffect();
    void SpawnApexEffect();
    void SpawnDiveTrailEffect(float deltaTime);
    void SpawnLandingEffect();
    /// 現在の攻撃段階に合わせて伸縮と回転を反映する。
    void ApplySlimeAnimation(float deltaTime);
    /// 見た目の変形と傾きを初期状態に戻す。
    void ResetVisualPose();
    /// 接地した見た目が地面から浮かないための位置補正を求める。
    engine::Vector3 CalculateGroundedVisualOffset(const engine::Vector3& visualScale) const;
    /// 急降下中の伸縮と傾きに応じた位置補正を求める。
    engine::Vector3 CalculateDiveVisualOffset(const engine::Vector3& visualScale, const engine::Vector3& visualRotation) const;

  private:
    // 各攻撃フェーズの時刻と、現在の飛行方向を保持します。
    MoveState moveState_ = MoveState::Wander;
    float jumpTimer_ = 0.0f;
    float idleTimer_ = 0.0f;
    float chargeTimer_ = 0.0f;
    float riseTimer_ = 0.0f;
    float diveTimer_ = 0.0f;
    float chargeEffectTimer_ = 0.0f;
    float chargeDebrisTimer_ = 0.0f;
    float chargeVortexTimer_ = 0.0f;
    float diveTrailTimer_ = 0.0f;
    float recoverTimer_ = 0.0f;
    float landingSquashTimer_ = 0.0f;
    float diveSpeed_ = 0.0f;
    engine::Vector3 diveDirection_ = {0.0f, 0.0f, 1.0f};
    engine::Vector3 baseScale_ = {2.0f, 2.0f, 2.0f};
    engine::Vector3 visualScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 visualScaleVelocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 visualRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 visualRotationVelocity_ = {0.0f, 0.0f, 0.0f};
    bool hasBaseScale_ = false;
    bool attackWarningTriggered_ = false;
};

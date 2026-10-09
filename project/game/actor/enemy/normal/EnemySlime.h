#pragma once
#include "BaseEnemy.h"

// 基礎変身の見本になる、溜めジャンプから急降下を行うピンクスライム。
class EnemySlime : public BaseEnemy {
  public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ApplyManagedScale(const Vector3& scale) override;
    const char* GetDebugMoveStateName() const;

  private:
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
    void EnsureBaseScale();
    Vector3 GetTargetPlanarDirection(float* outDistance = nullptr) const;
    Vector3 GetFacingPlanarDirection() const;
    void FaceDirection(const Vector3& direction, float turnRate);
    void UpdateWander(float deltaTime);
    void UpdateCharge(float deltaTime, const Vector3& direction, float distance);
    void BeginRise(const Vector3& direction);
    void UpdateRise(float deltaTime);
    void BeginDive(const Vector3& direction, float distance);
    void UpdateDive(float deltaTime);
    void BeginLandingRecovery();
    void UpdateLandingRecovery(float deltaTime);
    // 予備動作、飛行、着地の演出は判定処理と独立して発生させます。
    void SpawnChargePulseEffect(float deltaTime, float chargeRate);
    void SpawnChargeDebrisEffect(float deltaTime, float chargeRate);
    void SpawnChargeVortexEffect(float deltaTime, float chargeRate);
    void SpawnLaunchEffect();
    void SpawnApexEffect();
    void SpawnDiveTrailEffect(float deltaTime);
    void SpawnLandingEffect();
    void ApplySlimeAnimation(float deltaTime);
    void ResetVisualPose();
    Vector3 CalculateGroundedVisualOffset(const Vector3& visualScale) const;
    Vector3 CalculateDiveVisualOffset(const Vector3& visualScale, const Vector3& visualRotation) const;

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
    Vector3 diveDirection_ = {0.0f, 0.0f, 1.0f};
    Vector3 baseScale_ = {2.0f, 2.0f, 2.0f};
    Vector3 visualScale_ = {1.0f, 1.0f, 1.0f};
    Vector3 visualScaleVelocity_ = {0.0f, 0.0f, 0.0f};
    Vector3 visualRotation_ = {0.0f, 0.0f, 0.0f};
    Vector3 visualRotationVelocity_ = {0.0f, 0.0f, 0.0f};
    bool hasBaseScale_ = false;
    bool attackWarningTriggered_ = false;
};

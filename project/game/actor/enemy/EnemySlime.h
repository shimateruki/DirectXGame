#pragma once
#include "BaseEnemy.h"

// 基礎変身の見本になる、溜めジャンプから急降下を行うピンクスライム。
class EnemySlime : public BaseEnemy {
public:
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void ExecuteAbility(class Player* player) override;
    void UpdateCarriedAbility(class Player* player, float deltaTime) override;
    void CancelCarriedAbility(class Player* player);

private:
    enum class MoveState {
        Wander,
        Charge,
        Rise,
        Dive,
        Recover,
    };

    enum class CarriedAbilityState {
        Idle,
        Charge,
        Rise,
        Dive,
        Thrust,
        Recover,
    };

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
    void SpawnChargePulseEffect(float deltaTime, float chargeRate);
    void SpawnChargeDebrisEffect(float deltaTime, float chargeRate);
    void SpawnChargeVortexEffect(float deltaTime, float chargeRate);
    void SpawnLaunchEffect();
    void SpawnApexEffect();
    void SpawnDiveTrailEffect(float deltaTime);
    void SpawnLandingEffect();
    void ApplySlimeAnimation(float deltaTime);
    void BeginCarriedCharge(class Player* player);
    void UpdateCarriedCharge(class Player* player, float deltaTime);
    void BeginCarriedRise(class Player* player);
    void UpdateCarriedRise(class Player* player, float deltaTime);
    void BeginCarriedDive(class Player* player);
    void UpdateCarriedDive(class Player* player, float deltaTime);
    void EndCarriedDive(class Player* player);
    void BeginCarriedThrust(class Player* player);
    void UpdateCarriedThrust(class Player* player, float deltaTime);
    void ResetCarriedAbility(class Player* player, bool restoreControl);
    Vector3 GetPlayerDiveDirection(class Player* player) const;
    void SpawnCarriedChargeEffect(class Player* player, float deltaTime, float chargeRate);
    void SpawnCarriedDiveTrailEffect(class Player* player, float deltaTime);
    void SpawnCarriedLandingEffect(class Player* player);

private:
    MoveState moveState_ = MoveState::Wander;
    CarriedAbilityState carriedAbilityState_ = CarriedAbilityState::Idle;
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
    float carriedChargeTimer_ = 0.0f;
    float carriedRiseTimer_ = 0.0f;
    float carriedDiveTimer_ = 0.0f;
    float carriedRecoverTimer_ = 0.0f;
    float carriedChargeEffectTimer_ = 0.0f;
    float carriedDiveTrailTimer_ = 0.0f;
    float diveSpeed_ = 0.0f;
    Vector3 diveDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector3 carriedDiveDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector3 baseScale_ = { 2.0f, 2.0f, 2.0f };
    bool hasBaseScale_ = false;
};

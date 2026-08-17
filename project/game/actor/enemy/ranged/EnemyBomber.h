#pragma once
#include "BaseEnemy.h"
#include <functional>
#include <memory>

// 距離を保ちながら爆弾を生成して投げる、ボム使いの敵。
class EnemyBomber : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    void SetCarried(bool isCarried) override;
    void ExecuteAbility(class Player* player) override;
    void ExecutePlaceAbility(class Player* player);
    void ExecuteBlastJumpAbility(class Player* player);
    void UpdateCarriedAbility(class Player* player, float deltaTime) override;
    void ApplyManagedScale(const Vector3& scale) override {
        baseScale_ = scale;
        hasBaseScale_ = true;
        SetScale(scale);
    }

    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) {
        spawnCallback_ = callback;
    }

private:
    enum class ThrowState {
        Idle,
        Windup,
        LeapThrow,
        Landing
    };

    bool UpdateBlockedState(float deltaTime);
    void EnsureBaseScaleForAnimation();
    void UpdateThrowTimers(float deltaTime);
    void UpdateCombatMovement(float deltaTime, const Vector3& direction, float distance, bool inRange);
    void UpdateThrowState(float deltaTime, bool inRange);
    void ResetThrowStateAfterTargetLost();
    bool IsTargetInRange(float* outDistance = nullptr, Vector3* outDirection = nullptr) const;
    void UpdateFacing(const Vector3& direction);
    void UpdateFootwork(float deltaTime, const Vector3& direction, float distance, bool inRange);
    void ApplySlimeAnimation(float deltaTime);
    void UpdateHeldBombVisual(float deltaTime);
    void EnsureHeldBombVisual();
    void HideHeldBombVisual();
    Vector3 ComputeHeldBombPosition(float progress) const;
    float GetThrowProgress() const;
    float GetThrowLeapProgress() const;
    void BeginThrow();
    void BeginThrowLeap();
    void UpdateThrowLeap(float deltaTime);
    void BeginThrowLanding();
    void ThrowBomb();
    void ThrowCarryBomb(class Player* player);
    void PlaceCarryBomb(class Player* player);
    void DamageEnemiesWithBlastJump(class Player* player, const Vector3& center);
    void SpawnBombObject(std::unique_ptr<BaseEnemy> bomb);
    Vector3 GetPlayerForward(class Player* player) const;

    float throwTimer_ = 0.0f;
    float throwInterval_ = 2.45f;
    float windupTimer_ = 0.0f;
    float throwWindup_ = 1.05f;
    float initialThrowDelay_ = 0.55f;
    float footworkTimer_ = 0.0f;
    float footworkDirection_ = 1.0f;
    float carriedThrowCooldown_ = 0.0f;
    float carriedEffectTimer_ = 0.0f;
    float carriedPlaceCooldown_ = 0.0f;
    float carriedBlastJumpCooldown_ = 0.0f;
    float carriedBlastTrailTimer_ = 0.0f;
    float carriedBlastEffectTimer_ = 0.0f;
    float idleTimer_ = 0.0f;
    ThrowState throwState_ = ThrowState::Idle;
    float throwRecoilTimer_ = 0.0f;
    float throwRecoverPoseTimer_ = 0.0f;
    float throwLeapTimer_ = 0.0f;
    float throwLandingTimer_ = 0.0f;
    float throwLeapBaseYaw_ = 0.0f;
    float throwSpinDirection_ = 1.0f;
    bool throwBombReleased_ = false;
    std::function<void(std::unique_ptr<BaseEnemy>)> spawnCallback_ = nullptr;
    Object3dCommon* common_ = nullptr;
    Vector3 baseScale_ = { 2.0f, 2.0f, 2.0f };
    bool hasBaseScale_ = false;
    std::unique_ptr<Object3d> heldBombVisual_ = nullptr;
};




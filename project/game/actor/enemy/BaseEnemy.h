#pragma once
#include "Character.h"
#include <cstdint>

// 全ての敵の共通ルールを決めるクラス
class BaseEnemy : public Character {
public:
    virtual ~BaseEnemy() = default;

    // 初期化（モデル名を引数で受け取るように少し改良します）
    virtual void Initialize(Object3dCommon* common, const std::string& modelName);

    // 更新処理
    virtual void Update(float deltaTime) override;

    // 衝突判定
    virtual bool OnCollision(Object3d* other) override;

    // プレイヤーを追いかけるためにターゲットを登録する関数
    void SetTarget(Object3d* target) { target_ = target; }

    void SetDetectionRange(float range) { detectionRange_ = range; }
    float GetDetectionRange() const { return detectionRange_; }
    virtual void SetCarried(bool isCarried);
    bool IsCarried() const { return isCarried_; }
    virtual void BeginThrown(const Vector3& initialVelocity);
    bool IsThrownPhysics() const { return isThrownPhysics_; }
    virtual void ExecuteAbility(class Player* player);
    virtual void UpdateCarriedAbility(class Player* player, float deltaTime);


protected:
    Vector3 CalculateWanderVelocity(float deltaTime, float moveSpeed, float radiusScale = 0.65f, float verticalOffset = 0.0f, bool includeVertical = false);
    Vector3 GetWanderTargetPosition(float deltaTime, float radiusScale = 0.65f, float verticalOffset = 0.0f);
    void ResetWanderOrigin();
    bool IsThrowRecovering() const { return throwRecoveryTimer_ > 0.0f || isThrownPhysics_ || isThrowRotationRecovering_; }
    virtual void OnSlamImpact(const Vector3& impactPosition, float impactSpeed);

    Object3d* target_ = nullptr; // 追いかける対象（プレイヤー）
    float detectionRange_ = 20.0f; // 検知範囲
    float damageCooldownTimer_ = 0.0f; // 連続ヒットを防ぐ無敵タイマー
    float colorResetTimer_ = 0.0f;     // 赤色から元に戻すためのタイマー
    Vector4 defaultColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // 元の色を記憶
    bool isCarried_ = false;
    float throwRecoveryTimer_ = 0.0f;
    bool isThrownPhysics_ = false;

private:
    void CaptureWanderOrigin();
    void PickWanderTarget(float radiusScale, float verticalOffset);
    float NextWanderRandom();
    float GetWanderRadius(float radiusScale) const;
    void UpdateThrownPhysics(float deltaTime);
    void HandleThrownCollision(const CollisionInfo& info, uint32_t attribute);
    void EndThrownPhysics();
    void StartThrowRecovery();
    void UpdateThrowRecovery(float deltaTime);
    void SpawnSlamImpactEffect(const Vector3& impactPosition, float impactSpeed);
    void DamageSlamTargets(const Vector3& impactPosition, float impactSpeed);
    void UpdateDamageFeedbackTimers(float deltaTime);

    Vector3 wanderOrigin_ = { 0.0f, 0.0f, 0.0f };
    Vector3 wanderTarget_ = { 0.0f, 0.0f, 0.0f };
    float wanderWaitTimer_ = 0.0f;
    float wanderRetargetTimer_ = 0.0f;
    uint32_t wanderSeed_ = 0x12345678u;
    bool hasWanderOrigin_ = false;
    Vector3 thrownAngularVelocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 throwRecoveryStartRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 throwRecoveryTargetRotation_ = { 0.0f, 0.0f, 0.0f };
    float thrownTimer_ = 0.0f;
    float thrownSettleTimer_ = 0.0f;
    float throwRecoveryRotateTimer_ = 0.0f;
    float slamImpactCooldownTimer_ = 0.0f;
    int slamImpactCount_ = 0;
    bool isThrowRotationRecovering_ = false;
};

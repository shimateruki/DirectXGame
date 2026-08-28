#pragma once

#include "BaseEnemy.h"

#include <array>
#include <memory>

// 持続する突風で相手を押し流す、吹き飛ばし特化の属性スライムです。
class EnemyWindSlime : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool HasOwnedSpecialMaterialVisuals() const override;
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;
    void BeginThrown(const Vector3& initialVelocity) override;
    std::unique_ptr<Object3d> Clone() const override;


    void SetDebugPreviewAttackId(const std::string& attackId);
    const char* GetDebugAttackPhaseName() const;
    void ApplyManagedScale(const Vector3& scale) override;

private:
    enum class AttackState {
        Idle,
        GustWindup,
        GustActive,
        VolleyTakeoff,
        VolleyActive,
        VolleyLanding,
        Recover,
    };

    bool UpdateInactiveState(float deltaTime);
    bool UpdateThrowRecoveryState(float deltaTime);
    void EnsureBaseScale();
    void UpdateWildBehavior(float deltaTime, Vector3& velocity);
    void UpdateAttackState(float deltaTime, Vector3& velocity, const Vector3& direction, float distance);
    void StartGustBreath(const Vector3& direction);
    void StartAerialWindVolley(const Vector3& direction, float distance);
    void BeginRecover(const struct EnemyAttackDefinition& attack);
    void BeginVolleyLanding(const struct EnemyAttackDefinition& attack);
    void FireAerialWindOrb(int orbIndex);
    void UpdateVolleyImpactCenters();
    void ShowVolleyImpactTelegraphs(float progress);
    float ResolveVolleyHoverY();
    void HoldVolleyAltitude(float height, float deltaTime, Vector3& velocity);
    void EnsureHeldWindOrbs();
    void UpdateHeldWindOrbs(float deltaTime, float appearProgress = 1.0f);
    void HideHeldWindOrbs();
    Vector3 ComputeHeldWindOrbPosition(int orbIndex) const;
    void DispatchEnemyBreathPush(const Vector3& direction, float distance);
    void UpdateFacing(const Vector3& direction);
    void ApplySlimeAnimation(float deltaTime);

    void EmitWindBreathParticles(const Vector3& origin, const Vector3& direction, float range);
    void EmitWindPreset(const char* presetName, const Vector3& position);
    void EmitDirectedWindPreset(const char* presetName, const Vector3& position, const Vector3& direction, float speedScale);
    void SyncWorldCollisionRadius(float worldRadius);
    void SyncGroundCollisionRadius();
    void SyncThrownCollisionRadius();

    static constexpr int kWindAnimationPhaseCount = 8;

    AttackState attackState_ = AttackState::Idle;
    float attackStateTimer_ = 0.0f;
    float attackCooldown_ = 0.75f;
    float recoveryDuration_ = 0.0f;
    float idleTimer_ = 0.0f;
    float groundHopTimer_ = 0.0f;
    float ambientParticleTimer_ = 0.0f;
    float breathParticleTimer_ = 0.0f;
    float breathPushTimer_ = 0.0f;
    float lockedTargetDistance_ = 0.0f;
    float volleyGroundY_ = 0.0f;
    float volleyHoverY_ = 0.0f;
    float volleyLandingStartY_ = 0.0f;
    float volleyShotTimer_ = 0.0f;
    float volleyVisualTimer_ = 0.0f;
    float volleyRecoilTimer_ = 0.0f;
    Vector3 lockedAttackDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    int breathParticleCursor_ = 0;
    int volleyShotCount_ = 0;
    bool hasBaseScale_ = false;
    bool warningTriggered_ = false;
    std::string debugPreviewAttackId_;
    std::array<Vector3, 3> volleyImpactCenters_{};
    std::array<std::unique_ptr<Object3d>, 3> heldWindOrbVisuals_;
};

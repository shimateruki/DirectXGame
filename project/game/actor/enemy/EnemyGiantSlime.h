#pragma once
#include "BaseEnemy.h"

class EnemyGiantSlime : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;
    void BeginHookSplitPull(const Vector3& hookOwnerPos);
    bool UpdateHookSplitPull(float deltaTime, const Vector3& hookOwnerPos, class ParticleSystem* particleSystem);
    void CancelHookSplitPull();
    float GetHookSplitProgress() const;
    bool HasSplit() const { return hasSplit_; }

private:
    void LaunchJump(const Vector3& direction, float distance);
    void DispatchLandingShockwave();
    void ApplySlimeAnimation(float deltaTime);
    void SplitIntoSmallSlimes(class ParticleSystem* particleSystem);

    float jumpTimer_ = 0.0f;
    float landingPulseTimer_ = 0.0f;
    float idleTimer_ = 0.0f;
    float hookSplitPullTimer_ = 0.0f;
    bool isJumpingAttack_ = false;
    bool isHookSplitPulled_ = false;
    bool hasSplit_ = false;
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 hookSplitBasePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 hookSplitBaseScale_ = { 1.0f, 1.0f, 1.0f };
    bool hasBaseScale_ = false;
};

#pragma once

#include "engine/utility/math/Math.h"
#include "engine/animation/AnimatorController.h"
#include "engine/animation/BodyAnimationClip.h"

#include <string>
#include <unordered_map>

class Player;

class PlayerSlimeAnimator
{
public:
    enum class Mode {
        Idle,
        Run,
        Jump,
        Dash,
        AimHook,
        ShootHook,
        PullEnemy,
        Carry,
        GateReturn,
        Disabled
    };

    enum class AbilityPose {
        None,
        Guard,
        BlazeDash,
        SlowFall,
    };

    void Reset(const Vector3& baseScale, Player* player = nullptr);
    void SetMode(Mode mode);
    Mode GetMode() const { return mode_; }
    void SetMotionDirection(const Vector3& direction);
    void SetPullDirection(const Vector3& direction);
    void SetPullProgress(float progress);
    void SetJumpCharge(float chargeRate);
    void SetAbilityPose(AbilityPose pose, float strength = 1.0f);
    void TriggerImpulse(const Vector3& scale, float duration);
    // Animation Workbenchで編集できる能力モーションを、通常移動とは別レイヤーで再生します。
    bool PlayAbilityMotion(const std::string& clipName, bool loop = false,
        float playbackSpeed = 1.0f, float blendInDuration = 0.06f,
        float blendOutDuration = 0.10f);
    void StopAbilityMotion(float blendOutDuration = 0.08f);
    void ClearAbilityMotion(Player* player = nullptr);
    void Update(Player* player, float deltaTime);
    bool ReloadController();
    const AnimatorControllerAsset* GetControllerAsset() const { return controllerLoaded_ ? &controllerAsset_ : nullptr; }

private:
    Vector3 NormalizeOrForward(const Vector3& direction) const;
    Vector3 BuildModeScale(Player* player, float deltaTime) const;
    Vector3 BuildModeRotation(Player* player) const;
    bool TryBuildAuthoredBodyPose(Player* player, Vector3& scaleOut, Vector3& rotationOut,
        Vector3& visualOffsetOut) const;
    void UpdateAbilityMotion(float deltaTime, Vector3& visualScale,
        Vector3& visualRotation, Vector3& visualOffset);
    void ApplyAbilityPose(Vector3& scale, Vector3& rotation) const;
    void ReloadBodyClips();
    static const char* GetStateName(Mode mode);

    Mode mode_ = Mode::Idle;
    float modeTimer_ = 0.0f;
    float jumpChargeRate_ = 0.0f;
    float pullProgress_ = 0.0f;
    AbilityPose abilityPose_ = AbilityPose::None;
    float abilityPoseStrength_ = 0.0f;
    float impulseTimer_ = 0.0f;
    float impulseDuration_ = 0.0f;
    float modeTransitionTimer_ = 0.0f;
    bool modeTransitionCapturePending_ = false;
    Vector3 modeTransitionStartScale_ = { 2.0f, 2.0f, 2.0f };
    Vector3 modeTransitionStartRotation_ = { 0.0f, 0.0f, 0.0f };
    float jumpTakeoffStretchTimer_ = 0.0f;
    float landingHoldTimer_ = 0.0f;
    float landingSquashTimer_ = 0.0f;
    float landingReboundTimer_ = 0.0f;
    float landingImpactRate_ = 1.0f;
    float previousVelocityY_ = 0.0f;
    bool wasGrounded_ = true;
    Vector3 baseScale_ = { 2.0f, 2.0f, 2.0f };
    Vector3 motionDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector3 pullDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector3 impulseScale_ = { 2.0f, 2.0f, 2.0f };
    AnimatorControllerAsset controllerAsset_;
    AnimatorControllerRuntime controllerRuntime_;
    std::unordered_map<std::string, BodyAnimationClip> bodyClips_;
    BodyAnimationClip abilityMotionClip_;
    std::string abilityMotionName_;
    float abilityMotionTimer_ = 0.0f;
    float abilityMotionPlaybackSpeed_ = 1.0f;
    float abilityMotionBlendInDuration_ = 0.06f;
    float abilityMotionBlendOutDuration_ = 0.10f;
    float abilityMotionStopTimer_ = 0.0f;
    float abilityMotionStopDuration_ = 0.0f;
    bool abilityMotionActive_ = false;
    bool abilityMotionLoop_ = false;
    bool abilityMotionStopping_ = false;
    bool controllerLoaded_ = false;
};

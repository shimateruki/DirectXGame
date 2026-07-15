#pragma once

#include "engine/utility/math/Math.h"

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

    void Reset(const Vector3& baseScale);
    void SetMode(Mode mode);
    Mode GetMode() const { return mode_; }
    void SetMotionDirection(const Vector3& direction);
    void SetPullDirection(const Vector3& direction);
    void SetPullProgress(float progress);
    void SetJumpCharge(float chargeRate);
    void TriggerImpulse(const Vector3& scale, float duration);
    void Update(Player* player, float deltaTime);

private:
    Vector3 NormalizeOrForward(const Vector3& direction) const;
    Vector3 BuildModeScale(Player* player, float deltaTime) const;
    Vector3 BuildModeRotation(Player* player) const;

    Mode mode_ = Mode::Idle;
    float modeTimer_ = 0.0f;
    float jumpChargeRate_ = 0.0f;
    float pullProgress_ = 0.0f;
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
};

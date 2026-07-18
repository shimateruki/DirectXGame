#define NOMINMAX
#include "PlayerSlimeAnimator.h"
#include "Player.h"
#include "engine/utility/math/AnimationInterpolation.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kModeTransitionBlendDuration = 0.12f;
constexpr float kJumpTakeoffStretchDuration = 0.18f;
constexpr float kLandingHoldDuration = 0.08f;
constexpr float kLandingSquashDuration = 0.26f;
constexpr float kLandingReboundDuration = 0.32f;
constexpr float kGateReturnSlimeDuration = 1.12f;
constexpr const char* kPlayerSlimeControllerPath = "Resources/json/animator/player_slime.json";

float EaseOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

float HorizontalLength(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.z * v.z);
}

}

void PlayerSlimeAnimator::Reset(const Vector3& baseScale)
{
    if (!controllerLoaded_) {
        ReloadController();
    }
    baseScale_ = baseScale;
    mode_ = Mode::Idle;
    modeTimer_ = 0.0f;
    jumpChargeRate_ = 0.0f;
    pullProgress_ = 0.0f;
    impulseTimer_ = 0.0f;
    impulseDuration_ = 0.0f;
    impulseScale_ = baseScale_;
    modeTransitionTimer_ = 0.0f;
    modeTransitionCapturePending_ = false;
    modeTransitionStartScale_ = baseScale_;
    modeTransitionStartRotation_ = {};
    jumpTakeoffStretchTimer_ = 0.0f;
    landingHoldTimer_ = 0.0f;
    landingSquashTimer_ = 0.0f;
    landingReboundTimer_ = 0.0f;
    landingImpactRate_ = 1.0f;
    previousVelocityY_ = 0.0f;
    wasGrounded_ = true;
    if (controllerLoaded_) {
        controllerRuntime_.SetController(&controllerAsset_, true);
        controllerRuntime_.Play(GetStateName(Mode::Idle));
    }
}

void PlayerSlimeAnimator::SetMode(Mode mode)
{
    if (mode_ == mode) {
        return;
    }
    if (mode == Mode::Jump) {
        jumpTakeoffStretchTimer_ = kJumpTakeoffStretchDuration;
    }
    if (controllerLoaded_) {
        controllerRuntime_.CrossFade(GetStateName(mode));
        modeTransitionTimer_ = 0.0f;
    } else {
        modeTransitionTimer_ = kModeTransitionBlendDuration;
    }
    modeTransitionCapturePending_ = true;
    mode_ = mode;
    modeTimer_ = 0.0f;
    jumpChargeRate_ = 0.0f;
}

void PlayerSlimeAnimator::SetMotionDirection(const Vector3& direction)
{
    motionDirection_ = NormalizeOrForward(direction);
}

void PlayerSlimeAnimator::SetPullDirection(const Vector3& direction)
{
    pullDirection_ = NormalizeOrForward(direction);
}

void PlayerSlimeAnimator::SetPullProgress(float progress)
{
    pullProgress_ = std::clamp(progress, 0.0f, 1.0f);
}

void PlayerSlimeAnimator::SetJumpCharge(float chargeRate)
{
    jumpChargeRate_ = std::clamp(chargeRate, 0.0f, 1.0f);
}

void PlayerSlimeAnimator::TriggerImpulse(const Vector3& scale, float duration)
{
    impulseScale_ = scale;
    impulseDuration_ = (std::max)(duration, 0.01f);
    impulseTimer_ = impulseDuration_;
}

void PlayerSlimeAnimator::Update(Player* player, float deltaTime)
{
    if (!player || deltaTime <= 0.0f || mode_ == Mode::Disabled) {
        return;
    }

    if (modeTransitionCapturePending_) {
        modeTransitionStartScale_ = player->GetScale();
        modeTransitionStartRotation_ = player->GetRotation();
        modeTransitionCapturePending_ = false;
    }

    if (controllerLoaded_) {
        controllerRuntime_.Update(deltaTime, [](const std::string&) { return 0.0f; });
        modeTimer_ = controllerRuntime_.GetStateTime();
    } else {
        modeTimer_ += deltaTime;
    }
    const bool isGrounded = player->IsGrounded();
    const float verticalVelocity = player->GetVelocity().y;
    if (!wasGrounded_ && isGrounded) {
        const float jumpPower = (std::max)(player->GetJumpPower(), 0.01f);
        landingImpactRate_ = std::clamp(-previousVelocityY_ / jumpPower, 0.55f, 1.65f);
        landingHoldTimer_ = kLandingHoldDuration;
        landingSquashTimer_ = kLandingSquashDuration;
        landingReboundTimer_ = kLandingReboundDuration;
    }
    wasGrounded_ = isGrounded;
    if (landingHoldTimer_ > 0.0f) {
        landingHoldTimer_ = (std::max)(0.0f, landingHoldTimer_ - deltaTime);
    }
    else if (landingSquashTimer_ > 0.0f) {
        landingSquashTimer_ = (std::max)(0.0f, landingSquashTimer_ - deltaTime);
    }
    else if (landingReboundTimer_ > 0.0f) {
        landingReboundTimer_ = (std::max)(0.0f, landingReboundTimer_ - deltaTime);
    }
    if (jumpTakeoffStretchTimer_ > 0.0f) {
        jumpTakeoffStretchTimer_ = (std::max)(0.0f, jumpTakeoffStretchTimer_ - deltaTime);
    }
    previousVelocityY_ = verticalVelocity;

    Vector3 targetScale = BuildModeScale(player, deltaTime);
    if (impulseTimer_ > 0.0f) {
        impulseTimer_ = (std::max)(0.0f, impulseTimer_ - deltaTime);
        const float t = impulseTimer_ / (std::max)(impulseDuration_, 0.01f);
        const float rate = EaseOut(t);
        targetScale = {
            Math::Lerp(targetScale.x, impulseScale_.x, rate),
            Math::Lerp(targetScale.y, impulseScale_.y, rate),
            Math::Lerp(targetScale.z, impulseScale_.z, rate)
        };
    }

    Vector3 targetRotation = BuildModeRotation(player);
    const bool landingActive = landingSquashTimer_ > 0.0f || landingReboundTimer_ > 0.0f;
    const bool controllerTransition = controllerLoaded_ && controllerRuntime_.IsTransitioning();
    if (controllerTransition || modeTransitionTimer_ > 0.0f) {
        float blend = controllerTransition ? controllerRuntime_.GetTransitionWeight() : 1.0f;
        if (!controllerTransition) {
            const float progress = 1.0f - modeTransitionTimer_ / kModeTransitionBlendDuration;
            blend = AnimationInterpolation::ApplyEasing(
                progress,
                AnimationInterpolation::EasingType::SmootherStep);
        }
        player->SetScale(AnimationInterpolation::Lerp(modeTransitionStartScale_, targetScale, blend));
        player->SetRotation(AnimationInterpolation::SlerpEuler(modeTransitionStartRotation_, targetRotation, blend));
        if (!controllerTransition) {
            modeTransitionTimer_ = (std::max)(0.0f, modeTransitionTimer_ - deltaTime);
        }
    } else {
        float scaleFollowSpeed = 15.0f;
        if (landingActive) {
            scaleFollowSpeed = 92.0f;
        } else if (jumpTakeoffStretchTimer_ > 0.0f) {
            scaleFollowSpeed = 42.0f;
        } else if (mode_ == Mode::Jump) {
            scaleFollowSpeed = 20.0f;
        }
        player->SetScale(AnimationInterpolation::Damp(player->GetScale(), targetScale, scaleFollowSpeed, deltaTime));
        player->SetRotation(AnimationInterpolation::DampEuler(player->GetRotation(), targetRotation, 13.0f, deltaTime));
    }
    player->UpdateLocalMatrix();
    player->UpdateWorldMatrix();
}

bool PlayerSlimeAnimator::ReloadController()
{
    AnimatorControllerAsset asset;
    if (!asset.Load(kPlayerSlimeControllerPath)) {
        controllerLoaded_ = false;
        controllerRuntime_.SetController(nullptr, false);
        return false;
    }
    controllerAsset_ = std::move(asset);
    controllerLoaded_ = true;
    controllerRuntime_.SetController(&controllerAsset_, true);
    controllerRuntime_.Play(GetStateName(mode_));
    return true;
}

const char* PlayerSlimeAnimator::GetStateName(Mode mode)
{
    switch (mode) {
    case Mode::Idle: return "Idle";
    case Mode::Run: return "Run";
    case Mode::Jump: return "Jump";
    case Mode::Dash: return "Dash";
    case Mode::AimHook: return "AimHook";
    case Mode::ShootHook: return "ShootHook";
    case Mode::PullEnemy: return "PullEnemy";
    case Mode::Carry: return "Carry";
    case Mode::GateReturn: return "GateReturn";
    case Mode::Disabled: return "Disabled";
    }
    return "Idle";
}

Vector3 PlayerSlimeAnimator::NormalizeOrForward(const Vector3& direction) const
{
    Vector3 flat = { direction.x, 0.0f, direction.z };
    const float length = HorizontalLength(flat);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { flat.x / length, 0.0f, flat.z / length };
}

Vector3 PlayerSlimeAnimator::BuildModeScale(Player* player, float deltaTime) const
{
    (void)deltaTime;
    const Vector3 velocity = player ? player->GetVelocity() : Vector3{};
    const float speed = HorizontalLength(velocity);
    const float moveSpeed = player ? (std::max)(player->GetMoveSpeed(), 0.01f) : 1.0f;
    const float speedRate = std::clamp(speed / moveSpeed, 0.0f, 1.8f);
    Vector3 scale = baseScale_;

    switch (mode_) {
    case Mode::Idle: {
        const float breath = std::sin(modeTimer_ * 2.6f) * 0.066f;
        const float softPulse = std::sin(modeTimer_ * 5.6f + 0.7f) * 0.024f;
        const float skinRipple = std::sin(modeTimer_ * 10.5f + 1.1f) * 0.010f;
        scale.x = baseScale_.x * (1.0f + breath * 0.78f + softPulse + skinRipple);
        scale.y = baseScale_.y * (1.0f - breath * 1.12f - softPulse * 0.55f - std::abs(skinRipple) * 0.35f);
        scale.z = baseScale_.z * (1.0f + breath * 0.72f - softPulse * 0.38f - skinRipple * 0.60f);
        break;
    }
    case Mode::Run: {
        const float phase = modeTimer_ * (10.0f + speedRate * 4.5f);
        const float wave = std::sin(phase);
        const float squash = (std::max)(0.0f, -wave);
        const float stretch = (std::max)(0.0f, wave);
        const float sideWobble = std::sin(phase * 0.5f) * 0.044f;
        const float delayedRipple = std::sin(phase * 0.35f + 0.8f) * 0.026f * speedRate;
        scale.x = baseScale_.x * (1.0f + 0.055f * speedRate + squash * 0.20f - stretch * 0.09f + sideWobble + delayedRipple);
        scale.y = baseScale_.y * (1.0f - 0.040f * speedRate - squash * 0.22f + stretch * 0.28f - std::abs(delayedRipple) * 0.38f);
        scale.z = baseScale_.z * (1.0f + 0.060f * speedRate + squash * 0.15f - stretch * 0.06f - sideWobble * 0.70f - delayedRipple * 0.35f);
        break;
    }
    case Mode::Jump: {
        const float jumpPower = player ? (std::max)(player->GetJumpPower(), 0.01f) : 24.0f;
        const float apexRate = 1.0f - std::clamp(std::abs(velocity.y) / (jumpPower * 0.82f), 0.0f, 1.0f);
        const float airWave = std::sin(modeTimer_ * 12.5f);
        const float airRipple = std::sin(modeTimer_ * 21.0f + 0.8f);
        const float apexSquash = apexRate * (0.14f + std::abs(airWave) * 0.070f);
        const float airJelly = airWave * (0.050f + apexRate * 0.090f);
        const float sideJelly = airRipple * (0.034f + apexRate * 0.060f);

        if (jumpChargeRate_ > 0.0f) {
            const float pulse = std::sin(modeTimer_ * 22.0f) * 0.05f * jumpChargeRate_;
            const float squash = jumpChargeRate_ * 0.95f;
            scale.x = baseScale_.x * (1.0f + squash * 0.34f + pulse);
            scale.y = baseScale_.y * (1.0f - squash * 0.42f - std::abs(pulse) * 0.35f);
            scale.z = baseScale_.z * (1.0f + squash * 0.34f - pulse * 0.65f);
        } else if (velocity.y >= 0.0f) {
            const float stretch = std::clamp(velocity.y * 0.066f, 0.0f, 0.95f);
            scale.x = baseScale_.x * (1.0f - stretch * 0.32f + apexSquash + sideJelly);
            scale.y = baseScale_.y * (1.0f + stretch * 0.62f - apexSquash * 0.42f + airJelly * 0.72f);
            scale.z = baseScale_.z * (1.0f - stretch * 0.32f + apexSquash - sideJelly * 0.70f);
        } else {
            const float prepare = std::clamp(-velocity.y * 0.033f, 0.0f, 0.75f);
            const float fallRipple = std::sin(modeTimer_ * 13.5f) * 0.045f * prepare;
            scale.x = baseScale_.x * (1.0f + prepare * 0.42f + apexSquash * 0.65f + fallRipple + sideJelly);
            scale.y = baseScale_.y * (1.0f - prepare * 0.34f - apexSquash * 0.28f - std::abs(fallRipple) * 0.58f + airJelly * 0.55f);
            scale.z = baseScale_.z * (1.0f + prepare * 0.42f + apexSquash * 0.65f - fallRipple * 0.65f - sideJelly * 0.70f);
        }
        break;
    }
    case Mode::Dash: {
        const float startKick = 1.0f - std::clamp(modeTimer_ / 0.18f, 0.0f, 1.0f);
        const float flutter = std::sin(modeTimer_ * 42.0f) * 0.072f;
        scale.x = baseScale_.x * (0.78f + flutter);
        scale.y = baseScale_.y * (0.68f - startKick * 0.10f - std::abs(flutter) * 0.20f);
        scale.z = baseScale_.z * (1.46f + startKick * 0.25f - flutter);
        break;
    }
    case Mode::AimHook: {
        const float breath = std::sin(modeTimer_ * 7.0f) * 0.035f;
        scale.x = baseScale_.x * (1.13f + breath);
        scale.y = baseScale_.y * (0.73f - breath * 0.3f);
        scale.z = baseScale_.z * (1.13f + breath);
        break;
    }
    case Mode::ShootHook: {
        const float kick = 1.0f - std::clamp(modeTimer_ / 0.16f, 0.0f, 1.0f);
        scale.x = baseScale_.x * (1.12f + kick * 0.18f);
        scale.y = baseScale_.y * (0.82f - kick * 0.16f);
        scale.z = baseScale_.z * (1.10f + kick * 0.22f);
        break;
    }
    case Mode::PullEnemy: {
        const float strainPulse = std::abs(std::sin(modeTimer_ * 32.0f));
        const float tugWave = std::sin(modeTimer_ * 20.0f);
        const float pullSnap = std::sin(modeTimer_ * 46.0f) * 0.030f * (1.0f - pullProgress_);
        const float yankWave = (std::max)(0.0f, std::sin(modeTimer_ * 18.0f));
        const float yankPulse = yankWave * yankWave * (1.0f - pullProgress_ * 0.45f);
        const float strain = (0.22f + strainPulse * 0.18f) * (1.0f - pullProgress_ * 0.18f);
        scale.x = baseScale_.x * (1.0f + strain * 0.20f + tugWave * 0.024f + pullSnap - yankPulse * 0.045f);
        scale.y = baseScale_.y * (1.0f - strain * 0.42f - std::abs(tugWave) * 0.034f + yankPulse * 0.14f);
        scale.z = baseScale_.z * (1.0f + strain * 0.16f - tugWave * 0.016f - pullSnap * 0.46f - yankPulse * 0.045f);
        break;
    }
    case Mode::Carry: {
        const float wobble = std::sin(modeTimer_ * 11.0f) * 0.075f;
        const float strain = std::abs(std::sin(modeTimer_ * 5.5f)) * 0.045f;
        scale.x = baseScale_.x * (1.05f + strain - wobble * 0.45f);
        scale.y = baseScale_.y * (0.95f - strain * 0.4f + wobble);
        scale.z = baseScale_.z * (1.05f + strain - wobble * 0.45f);
        break;
    }
    case Mode::GateReturn: {
        const float t = std::clamp(modeTimer_ / kGateReturnSlimeDuration, 0.0f, 1.0f);
        const float squeeze = 1.0f - std::clamp(t / 0.36f, 0.0f, 1.0f);
        const float stretch = std::sin(std::clamp((t - 0.18f) / 0.48f, 0.0f, 1.0f) * kPi);
        const float settle = std::sin(std::clamp((t - 0.55f) / 0.45f, 0.0f, 1.0f) * kPi * 2.0f) * (1.0f - t);

        scale.x = baseScale_.x * (1.0f + squeeze * 0.24f + stretch * 0.08f + settle * 0.045f);
        scale.y = baseScale_.y * (1.0f - squeeze * 0.30f + stretch * 0.18f - settle * 0.060f);
        scale.z = baseScale_.z * (1.0f + squeeze * 0.32f + stretch * 0.10f - settle * 0.035f);
        break;
    }
    case Mode::Disabled:
        break;
    }

    if (jumpTakeoffStretchTimer_ > 0.0f) {
        const float t = jumpTakeoffStretchTimer_ / kJumpTakeoffStretchDuration;
        const float stretch = t * t * 0.34f;
        scale.x *= 1.0f - stretch * 0.38f;
        scale.y *= 1.0f + stretch * 0.82f;
        scale.z *= 1.0f - stretch * 0.38f;
    }

    if (landingSquashTimer_ > 0.0f) {
        const float t = landingSquashTimer_ / kLandingSquashDuration;
        const float squash = std::clamp(t * t * 0.72f * landingImpactRate_, 0.0f, 0.82f);
        scale.x *= 1.0f + squash * 1.08f;
        scale.y *= (std::max)(0.38f, 1.0f - squash * 0.92f);
        scale.z *= 1.0f + squash * 1.08f;
    }
    else if (landingReboundTimer_ > 0.0f) {
        const float t = 1.0f - (landingReboundTimer_ / kLandingReboundDuration);
        const float damping = std::pow(1.0f - t, 1.25f);
        const float rebound = std::sin(t * kPi * 2.5f) * damping * 0.20f * landingImpactRate_;
        scale.x *= 1.0f - rebound * 0.45f;
        scale.y *= 1.0f + rebound;
        scale.z *= 1.0f - rebound * 0.45f;
    }

    scale.x = (std::max)(scale.x, 0.25f);
    scale.y = (std::max)(scale.y, 0.25f);
    scale.z = (std::max)(scale.z, 0.25f);
    return scale;
}

Vector3 PlayerSlimeAnimator::BuildModeRotation(Player* player) const
{
    Vector3 current = player ? player->GetRotation() : Vector3{};
    Vector3 rotation = { 0.0f, current.y, 0.0f };
    const Vector3 velocity = player ? player->GetVelocity() : Vector3{};
    const float speed = HorizontalLength(velocity);

    switch (mode_) {
    case Mode::Idle:
        rotation.x = std::sin(modeTimer_ * 2.2f) * 0.014f;
        rotation.z = std::cos(modeTimer_ * 2.0f) * 0.014f;
        break;
    case Mode::Run:
    case Mode::Dash: {
        Vector3 dir = speed > 0.01f ? NormalizeOrForward(velocity) : motionDirection_;
        const float lean = mode_ == Mode::Dash ? 0.18f : std::clamp(speed * 0.016f, 0.0f, 0.16f);
        rotation.x = -dir.z * lean;
        rotation.z = dir.x * lean;
        break;
    }
    case Mode::Jump: {
        Vector3 dir = speed > 0.01f ? NormalizeOrForward(velocity) : motionDirection_;
        const float lean = std::clamp(speed * 0.008f, 0.0f, 0.12f);
        const float airTilt = 1.0f - std::clamp(std::abs(velocity.y) / 24.0f, 0.0f, 1.0f);
        rotation.x = -dir.z * lean + std::sin(modeTimer_ * 7.0f) * (0.038f + airTilt * 0.040f);
        rotation.z = dir.x * lean + std::cos(modeTimer_ * 6.5f) * (0.036f + airTilt * 0.036f);
        break;
    }
    case Mode::AimHook:
        rotation.x = current.x;
        rotation.z = 0.0f;
        break;
    case Mode::ShootHook:
        rotation.x = -motionDirection_.z * 0.08f;
        rotation.z = motionDirection_.x * 0.08f;
        break;
    case Mode::PullEnemy: {
        const float yankWave = (std::max)(0.0f, std::sin(modeTimer_ * 18.0f));
        const float yankPulse = yankWave * yankWave * (1.0f - pullProgress_ * 0.45f);
        const float brace = 1.0f - pullProgress_ * 0.55f;
        const float nodShake = std::sin(modeTimer_ * 30.0f) * 0.035f * brace;
        rotation.x = 0.16f * brace + yankPulse * 0.36f + nodShake;
        rotation.z = 0.0f;
        break;
    }
    case Mode::Carry:
        rotation.x = std::sin(modeTimer_ * 7.0f) * 0.025f;
        rotation.z = std::cos(modeTimer_ * 6.0f) * 0.025f;
        break;
    case Mode::GateReturn: {
        const float t = std::clamp(modeTimer_ / kGateReturnSlimeDuration, 0.0f, 1.0f);
        const float lean = std::sin(std::clamp(t / 0.70f, 0.0f, 1.0f) * kPi) * (1.0f - t * 0.55f) * 0.16f;
        rotation.x = -motionDirection_.z * lean;
        rotation.z = motionDirection_.x * lean;
        break;
    }
    default:
        break;
    }

    return rotation;
}

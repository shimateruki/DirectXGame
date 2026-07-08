#define NOMINMAX
#include "PlayerSlimeAnimator.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kLandingSquashDuration = 0.16f;
constexpr float kGateReturnSlimeDuration = 1.12f;

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
    baseScale_ = baseScale;
    mode_ = Mode::Idle;
    modeTimer_ = 0.0f;
    jumpChargeRate_ = 0.0f;
    pullProgress_ = 0.0f;
    impulseTimer_ = 0.0f;
    impulseDuration_ = 0.0f;
    impulseScale_ = baseScale_;
    landingSquashTimer_ = 0.0f;
    wasGrounded_ = true;
}

void PlayerSlimeAnimator::SetMode(Mode mode)
{
    if (mode_ == mode) {
        return;
    }
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

    modeTimer_ += deltaTime;
    const bool isGrounded = player->IsGrounded();
    if (!wasGrounded_ && isGrounded) {
        landingSquashTimer_ = kLandingSquashDuration;
    }
    wasGrounded_ = isGrounded;
    if (landingSquashTimer_ > 0.0f) {
        landingSquashTimer_ = (std::max)(0.0f, landingSquashTimer_ - deltaTime);
    }

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

    const float scaleAlpha = 1.0f - std::exp(-24.0f * deltaTime);
    Vector3 currentScale = player->GetScale();
    player->SetScale({
        Math::Lerp(currentScale.x, targetScale.x, scaleAlpha),
        Math::Lerp(currentScale.y, targetScale.y, scaleAlpha),
        Math::Lerp(currentScale.z, targetScale.z, scaleAlpha)
    });

    Vector3 targetRotation = BuildModeRotation(player);
    Vector3 currentRotation = player->GetRotation();
    const float rotationAlpha = 1.0f - std::exp(-13.0f * deltaTime);
    player->SetRotation({
        Math::Lerp(currentRotation.x, targetRotation.x, rotationAlpha),
        currentRotation.y,
        Math::Lerp(currentRotation.z, targetRotation.z, rotationAlpha)
    });
    player->UpdateLocalMatrix();
    player->UpdateWorldMatrix();
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
        const float breath = std::sin(modeTimer_ * 3.0f) * 0.055f;
        const float softPulse = std::sin(modeTimer_ * 6.0f + 0.7f) * 0.018f;
        scale.x = baseScale_.x * (1.0f + breath * 0.70f + softPulse);
        scale.y = baseScale_.y * (1.0f - breath * 1.05f - softPulse * 0.45f);
        scale.z = baseScale_.z * (1.0f + breath * 0.70f - softPulse * 0.35f);
        break;
    }
    case Mode::Run: {
        const float phase = modeTimer_ * (10.0f + speedRate * 4.5f);
        const float wave = std::sin(phase);
        const float squash = (std::max)(0.0f, -wave);
        const float stretch = (std::max)(0.0f, wave);
        const float sideWobble = std::sin(phase * 0.5f) * 0.035f;
        scale.x = baseScale_.x * (1.0f + 0.07f * speedRate + squash * 0.16f - stretch * 0.07f + sideWobble);
        scale.y = baseScale_.y * (1.0f - 0.05f * speedRate - squash * 0.18f + stretch * 0.24f);
        scale.z = baseScale_.z * (1.0f + 0.04f * speedRate + squash * 0.12f - stretch * 0.05f - sideWobble * 0.6f);
        break;
    }
    case Mode::Jump: {
        if (jumpChargeRate_ > 0.0f) {
            const float pulse = std::sin(modeTimer_ * 22.0f) * 0.05f * jumpChargeRate_;
            const float squash = jumpChargeRate_ * 0.95f;
            scale.x = baseScale_.x * (1.0f + squash * 0.34f + pulse);
            scale.y = baseScale_.y * (1.0f - squash * 0.42f - std::abs(pulse) * 0.35f);
            scale.z = baseScale_.z * (1.0f + squash * 0.34f - pulse * 0.65f);
        } else if (velocity.y >= 0.0f) {
            const float stretch = std::clamp(velocity.y * 0.050f, 0.0f, 0.72f);
            scale.x = baseScale_.x * (1.0f - stretch * 0.20f);
            scale.y = baseScale_.y * (1.0f + stretch * 0.38f);
            scale.z = baseScale_.z * (1.0f - stretch * 0.20f);
        } else {
            const float prepare = std::clamp(-velocity.y * 0.022f, 0.0f, 0.42f);
            scale.x = baseScale_.x * (1.0f + prepare * 0.25f);
            scale.y = baseScale_.y * (1.0f - prepare * 0.18f);
            scale.z = baseScale_.z * (1.0f + prepare * 0.25f);
        }
        break;
    }
    case Mode::Dash: {
        const float startKick = 1.0f - std::clamp(modeTimer_ / 0.18f, 0.0f, 1.0f);
        const float flutter = std::sin(modeTimer_ * 38.0f) * 0.06f;
        scale.x = baseScale_.x * (0.82f + flutter);
        scale.y = baseScale_.y * (0.72f - startKick * 0.08f);
        scale.z = baseScale_.z * (1.38f + startKick * 0.22f - flutter);
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
        const float strainPulse = std::abs(std::sin(modeTimer_ * 28.0f));
        const float strain = (0.20f + strainPulse * 0.18f) * (1.0f - pullProgress_ * 0.35f);
        scale.x = baseScale_.x * (1.0f + strain);
        scale.y = baseScale_.y * (1.0f - strain * 0.62f);
        scale.z = baseScale_.z * (1.0f + strain * 0.72f);
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

    if (landingSquashTimer_ > 0.0f) {
        const float t = landingSquashTimer_ / kLandingSquashDuration;
        const float squash = std::sin(t * kPi) * 0.22f;
        scale.x *= 1.0f + squash;
        scale.y *= 1.0f - squash * 0.72f;
        scale.z *= 1.0f + squash;
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
        const float lean = std::clamp(speed * 0.006f, 0.0f, 0.08f);
        rotation.x = -dir.z * lean;
        rotation.z = dir.x * lean;
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
        const float lean = 0.16f * (1.0f - pullProgress_ * 0.2f);
        rotation.x = pullDirection_.z * lean;
        rotation.z = -pullDirection_.x * lean;
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

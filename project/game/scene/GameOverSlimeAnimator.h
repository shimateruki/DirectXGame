#pragma once

#include "Object3d.h"
#include "engine/utility/math/Math.h"

#include <algorithm>
#include <cmath>

/// <summary>
/// ゲームオーバー時のスライム演出を管理するクラス。
/// GameOverScene側で直接スケールや回転をいじり続けないための分離先。
/// </summary>
class GameOverSlimeAnimator {
public:
    void Reset(Object3d* slime) {
        timer_ = 0.0f;
        exitTimer_ = 0.0f;
        phase_ = Phase::Downed;
        hasBaseTransform_ = false;
        baseRotation_ = { 0.0f, 0.0f, 0.0f };
        currentPosition_ = { 0.0f, 0.0f, 0.0f };
        currentScale_ = { 1.0f, 1.0f, 1.0f };
        currentRotation_ = { 0.0f, 0.0f, 0.0f };

        if (slime) {
            CaptureBaseTransform(slime);
            ApplyMaterial(slime);
        }
    }

    void Update(Object3d* slime, float deltaTime) {
        if (!slime) {
            return;
        }

        if (!hasBaseTransform_) {
            CaptureBaseTransform(slime);
        }

        ApplyMaterial(slime);
        if (phase_ == Phase::ExitRight || phase_ == Phase::ExitFinished) {
            UpdateExitRight(slime, deltaTime);
            return;
        }

        timer_ += deltaTime;
        constexpr float kFallSettleDuration = 1.42f;
        const float settle = EaseOutCubic(timer_ / kFallSettleDuration);
        const float sleepBob = std::sin(timer_ * 0.92f) * 0.012f;
        const float faintPulse = 0.5f + 0.5f * std::sin(timer_ * 1.25f);
        const float impact = ImpactPulse(timer_);

        const Vector3 startPosition = {
            basePosition_.x,
            basePosition_.y + kPresentationYOffset + 0.16f,
            basePosition_.z
        };
        const Vector3 downedPosition = {
            basePosition_.x,
            basePosition_.y + kPresentationYOffset + sleepBob - impact * 0.025f,
            basePosition_.z
        };

        const Vector3 startScale = baseScale_;
        const Vector3 downedScale = {
            baseScale_.x * (1.18f + impact * 0.10f - faintPulse * 0.025f),
            baseScale_.y * (0.58f - impact * 0.045f + faintPulse * 0.018f),
            baseScale_.z * (1.06f + impact * 0.06f)
        };

        const Vector3 startRotation = {
            baseRotation_.x + 0.08f,
            baseRotation_.y,
            baseRotation_.z
        };
        const Vector3 downedRotation = {
            baseRotation_.x + 3.14159265f + std::sin(timer_ * 0.55f) * 0.035f + impact * 0.06f,
            baseRotation_.y + std::sin(timer_ * 0.35f) * 0.02f,
            baseRotation_.z + 0.08f + std::sin(timer_ * 0.80f) * 0.025f + impact * 0.04f
        };

        currentPosition_ = Math::Lerp(startPosition, downedPosition, settle);
        currentScale_ = Math::Lerp(startScale, downedScale, settle);
        currentRotation_ = Math::Lerp(startRotation, downedRotation, settle);

        slime->SetTranslate(currentPosition_);
        slime->SetScale(currentScale_);
        slime->SetRotation(currentRotation_);
        slime->SetColor({ 0.78f, 0.94f, 1.0f, 1.0f });
        slime->SetEmissive(1.0f + faintPulse * 0.08f);
        slime->UpdateLocalMatrix();
        slime->UpdateWorldMatrix();
    }

    void StartExitRight(Object3d* slime, const Vector3& direction, float distance) {
        if (!slime) {
            phase_ = Phase::ExitFinished;
            return;
        }

        if (!hasBaseTransform_) {
            CaptureBaseTransform(slime);
        }

        exitTimer_ = 0.0f;
        exitStartPosition_ = currentPosition_;
        exitStartScale_ = currentScale_;
        exitStartRotation_ = currentRotation_;
        exitDirection_ = direction;
        exitDirection_.y = 0.0f;
        exitDirection_.z = 0.0f;

        const float length = std::sqrt(exitDirection_.x * exitDirection_.x + exitDirection_.z * exitDirection_.z);
        if (length > 0.001f) {
            exitDirection_.x /= length;
            exitDirection_.z /= length;
        }
        else {
            exitDirection_ = { 1.0f, 0.0f, 0.0f };
        }

        exitDistance_ = distance;
        phase_ = Phase::ExitRight;
    }

    bool IsExiting() const {
        return phase_ == Phase::ExitRight || phase_ == Phase::ExitFinished;
    }

    bool IsExitAnimationFinished() const {
        return phase_ == Phase::ExitFinished;
    }

    float GetDizzyAlpha() const {
        if (phase_ == Phase::Downed) {
            return 1.0f;
        }
        if (phase_ == Phase::ExitRight) {
            return 1.0f - Clamp01(exitTimer_ / 0.46f);
        }
        return 0.0f;
    }

    Vector3 GetDizzyAnchorWorld() const {
        const float yOffset = (std::max)(0.26f, currentScale_.y * 0.46f) + 0.10f;
        return {
            currentPosition_.x,
            currentPosition_.y + yOffset,
            currentPosition_.z
        };
    }

    float GetTimer() const { return timer_; }
    bool HasBaseTransform() const { return hasBaseTransform_; }

private:
    static constexpr float kPresentationYOffset = 0.34f;

    enum class Phase {
        Downed,
        ExitRight,
        ExitFinished
    };

    static float Clamp01(float value) {
        return std::clamp(value, 0.0f, 1.0f);
    }

    static float EaseOutCubic(float t) {
        t = Clamp01(t);
        const float inv = 1.0f - t;
        return 1.0f - inv * inv * inv;
    }

    static float ImpactPulse(float time) {
        const float center = 0.88f;
        const float width = 0.36f;
        const float rate = 1.0f - Clamp01(std::abs(time - center) / width);
        return rate * rate;
    }

    static float EaseInOutCubic(float t) {
        t = Clamp01(t);
        if (t < 0.5f) {
            return 4.0f * t * t * t;
        }
        const float f = -2.0f * t + 2.0f;
        return 1.0f - (f * f * f) * 0.5f;
    }

    static float LerpFloat(float start, float end, float t) {
        return start + (end - start) * Clamp01(t);
    }

    static float EstimateHalfHeight(const Vector3& scale) {
        return scale.y * 0.5f;
    }

    static float GroundedCenterY(
        const Vector3& startPosition,
        const Vector3& startScale,
        const Vector3& endPosition,
        const Vector3& endScale,
        const Vector3& currentScale,
        float t) {
        const float startBottom = startPosition.y - EstimateHalfHeight(startScale);
        const float endBottom = endPosition.y - EstimateHalfHeight(endScale);
        const float bottom = LerpFloat(startBottom, endBottom, EaseInOutCubic(t));
        return bottom + EstimateHalfHeight(currentScale);
    }

    void UpdateExitRight(Object3d* slime, float deltaTime) {
        if (phase_ == Phase::ExitFinished) {
            return;
        }

        exitTimer_ += deltaTime;

        constexpr float kStandDuration = 0.72f;
        constexpr float kRunDuration = 1.90f;
        constexpr float kTotalDuration = kStandDuration + kRunDuration;

        Vector3 position = exitStartPosition_;
        Vector3 scale = exitStartScale_;
        Vector3 rotation = exitStartRotation_;

        if (exitTimer_ < kStandDuration) {
            const float rate = EaseOutCubic(exitTimer_ / kStandDuration);
            const float standBounce = std::sin(rate * 3.14159265f) * 0.055f;

            const Vector3 standPosition = {
                basePosition_.x,
                basePosition_.y + kPresentationYOffset + standBounce,
                basePosition_.z
            };
            const Vector3 standScale = {
                baseScale_.x * (1.0f - standBounce * 0.35f),
                baseScale_.y * (1.0f + standBounce * 0.70f),
                baseScale_.z * (1.0f - standBounce * 0.20f)
            };
            const Vector3 standRotation = baseRotation_;

            position = Math::Lerp(exitStartPosition_, standPosition, rate);
            scale = Math::Lerp(exitStartScale_, standScale, rate);
            rotation = Math::Lerp(exitStartRotation_, standRotation, rate);
            position.y = LerpFloat(exitStartPosition_.y, basePosition_.y + kPresentationYOffset, rate) + standBounce;
        }
        else {
            const float runTime = exitTimer_ - kStandDuration;
            const float rate = Clamp01(runTime / kRunDuration);
            const float moveRate = EaseInOutCubic(rate);
            const float hopWave = std::abs(std::sin(rate * 3.14159265f * 4.0f));
            const float landingWave = 1.0f - hopWave;
            const Vector3 moveOffset = exitDirection_ * (exitDistance_ * moveRate);
            const float lowBounce = hopWave * 0.045f;

            position = {
                basePosition_.x + moveOffset.x,
                exitStartPosition_.y + lowBounce,
                basePosition_.z + moveOffset.z
            };
            scale = {
                baseScale_.x * (1.0f + landingWave * 0.10f - hopWave * 0.035f),
                baseScale_.y * (1.0f - landingWave * 0.10f + hopWave * 0.045f),
                baseScale_.z * (1.0f + landingWave * 0.07f - hopWave * 0.030f)
            };
            rotation = {
                baseRotation_.x + std::sin(rate * 3.14159265f * 4.0f) * 0.10f,
                std::atan2(exitDirection_.x, exitDirection_.z),
                baseRotation_.z - 0.10f + std::sin(rate * 3.14159265f * 8.0f) * 0.08f
            };
        }

        currentPosition_ = position;
        currentScale_ = scale;
        currentRotation_ = rotation;

        slime->SetTranslate(currentPosition_);
        slime->SetScale(currentScale_);
        slime->SetRotation(currentRotation_);
        slime->SetColor({ 0.78f, 0.94f, 1.0f, 1.0f });
        slime->SetEmissive(1.05f);
        slime->UpdateLocalMatrix();
        slime->UpdateWorldMatrix();

        if (exitTimer_ >= kTotalDuration) {
            phase_ = Phase::ExitFinished;
        }
    }

    void CaptureBaseTransform(Object3d* slime) {
        basePosition_ = slime->GetWorldPosition();
        baseScale_ = slime->GetScale();
        baseRotation_ = slime->GetRotation();
        currentPosition_ = basePosition_;
        currentScale_ = baseScale_;
        currentRotation_ = baseRotation_;
        hasBaseTransform_ = true;
    }

    void ApplyMaterial(Object3d* slime) {
        slime->SetEnableLighting(false);
        slime->SetBlendMode(BlendMode::kNormal);
    }

private:
    Vector3 basePosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 currentPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 currentScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 currentRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 exitStartPosition_ = { 0.0f, 0.0f, 0.0f };
    Vector3 exitStartScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 exitStartRotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 exitDirection_ = { 1.0f, 0.0f, 0.0f };
    float exitDistance_ = 10.0f;
    float timer_ = 0.0f;
    float exitTimer_ = 0.0f;
    bool hasBaseTransform_ = false;
    Phase phase_ = Phase::Downed;
};

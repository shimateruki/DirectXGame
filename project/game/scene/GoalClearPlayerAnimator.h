#pragma once

#include "Player.h"
#include "engine/utility/math/AnimationInterpolation.h"

#include <algorithm>
#include <cmath>

// 王冠取得時のプレイヤー勝利モーションを管理します。
// 位置と拡縮は滑らかな5次補間、回転はクォータニオン補間で接続します。
class GoalClearPlayerAnimator {
public:
    struct Tuning {
        float crownLandTime = 0.95f;
        float anticipationStartTime = 1.04f;
        float jumpStartTime = 1.24f;
        float apexTime = 1.92f;
        float resultUiTime = 2.00f;
        float readyTime = 2.75f;
        float jumpHeight = 3.25f;
        float forwardDistance = 0.45f;
        float anticipationDepth = 0.22f;
        float landingSquash = 0.16f;
        float anticipationSquash = 0.28f;
        float takeoffStretch = 0.34f;
        float resultStretch = 0.12f;
        float resultYawBias = 0.10f;
    };

    bool Start(Player* player, const Vector3& crownPosition, const Vector3& preferredForward = Vector3{}) {
        player_ = player;
        if (!player_) {
            isActive_ = false;
            return false;
        }

        savedControlActive_ = player_->IsControlActive();
        basePosition_ = player_->GetTranslate();
        baseScale_ = player_->GetScale();
        baseRotation_ = player_->GetRotation();
        posePosition_ = basePosition_;
        currentScale_ = baseScale_;

        player_->SetIsControlActive(false);
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });

        const Vector3 crownForward = NormalizePlanarDirection(
            { crownPosition.x - basePosition_.x, 0.0f, crownPosition.z - basePosition_.z },
            DirectionFromYaw(baseRotation_.y - player_->GetVisualYawOffset()));
        const Vector3 desiredForward = NormalizePlanarDirection(preferredForward, crownForward);
        const float faceYaw = std::atan2(desiredForward.x, desiredForward.z);
        baseRotation_.y = NormalizeYaw(faceYaw + player_->GetVisualYawOffset());
        resultRotation_ = baseRotation_;
        resultRotation_.y = NormalizeYaw(resultRotation_.y + tuning_.resultYawBias);
        moveForward_ = desiredForward;
        moveRight_ = RightFromForward(moveForward_);

        player_->SetRotation(baseRotation_);
        isActive_ = true;
        return true;
    }

    void Reset() {
        player_ = nullptr;
        isActive_ = false;
        savedControlActive_ = true;
        basePosition_ = {};
        baseScale_ = { 1.0f, 1.0f, 1.0f };
        baseRotation_ = {};
        resultRotation_ = {};
        posePosition_ = basePosition_;
        currentScale_ = baseScale_;
        moveForward_ = { 0.0f, 0.0f, 1.0f };
        moveRight_ = { 1.0f, 0.0f, 0.0f };
    }

    void RestoreInitialPose() {
        if (!player_ || !isActive_) {
            return;
        }
        player_->SetTranslate(basePosition_);
        player_->SetScale(baseScale_);
        player_->SetRotation(baseRotation_);
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
        player_->UpdateLocalMatrix();
        player_->UpdateWorldMatrix();
    }

    void RestoreControl() {
        if (player_) {
            player_->SetIsControlActive(savedControlActive_);
        }
    }

    void Update(float time) {
        if (!player_ || !isActive_) {
            return;
        }

        const Tuning& c = tuning_;
        Vector3 position = basePosition_;
        Vector3 scale = baseScale_;
        Vector3 rotation = baseRotation_;

        if (time < c.crownLandTime) {
            // 王冠へ注目している間は、プレイヤーを安定した待機姿勢に固定します。
        } else if (time < c.anticipationStartTime) {
            const float p = AnimationInterpolation::SegmentRate(time, c.crownLandTime, c.anticipationStartTime);
            const float impact = std::sin(p * kPi) * (1.0f - p * 0.45f);
            scale.x = baseScale_.x * (1.0f + c.landingSquash * impact);
            scale.y = baseScale_.y * (1.0f - c.landingSquash * 0.78f * impact);
            scale.z = baseScale_.z * (1.0f + c.landingSquash * impact);
        } else if (time < c.jumpStartTime) {
            const float p = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(time, c.anticipationStartTime, c.jumpStartTime),
                AnimationInterpolation::EasingType::SmootherStep);
            position.y -= c.anticipationDepth * p;
            scale.x = baseScale_.x * (1.0f + c.anticipationSquash * p);
            scale.y = baseScale_.y * (1.0f - c.anticipationSquash * 0.92f * p);
            scale.z = baseScale_.z * (1.0f + c.anticipationSquash * p);
        } else if (time < c.apexTime) {
            const float raw = AnimationInterpolation::SegmentRate(time, c.jumpStartTime, c.apexTime);
            const float travel = AnimationInterpolation::ApplyEasing(raw, AnimationInterpolation::EasingType::SmootherStep);
            position.x += moveForward_.x * c.forwardDistance * travel;
            position.z += moveForward_.z * c.forwardDistance * travel;
            position.y += AnimationInterpolation::Lerp(-c.anticipationDepth, c.jumpHeight, travel);

            Vector3 squashScale = {
                baseScale_.x * (1.0f + c.anticipationSquash),
                baseScale_.y * (1.0f - c.anticipationSquash * 0.92f),
                baseScale_.z * (1.0f + c.anticipationSquash)
            };
            Vector3 stretchScale = {
                baseScale_.x * (1.0f - c.takeoffStretch * 0.48f),
                baseScale_.y * (1.0f + c.takeoffStretch),
                baseScale_.z * (1.0f - c.takeoffStretch * 0.48f)
            };
            Vector3 resultScale = {
                baseScale_.x * (1.0f - c.resultStretch * 0.36f),
                baseScale_.y * (1.0f + c.resultStretch),
                baseScale_.z * (1.0f - c.resultStretch * 0.36f)
            };

            if (raw < 0.24f) {
                const float stretchRate = AnimationInterpolation::ApplyEasing(raw / 0.24f, AnimationInterpolation::EasingType::EaseOut);
                scale = AnimationInterpolation::Lerp(squashScale, stretchScale, stretchRate);
            } else {
                const float settleRate = AnimationInterpolation::ApplyEasing((raw - 0.24f) / 0.76f, AnimationInterpolation::EasingType::SmootherStep);
                scale = AnimationInterpolation::Lerp(stretchScale, resultScale, settleRate);
            }

            rotation = AnimationInterpolation::SlerpEuler(baseRotation_, resultRotation_, travel);
            const float airLean = std::sin(raw * kPi) * 0.075f;
            rotation.x -= moveForward_.z * airLean;
            rotation.z += moveForward_.x * airLean;
        } else {
            position.x += moveForward_.x * c.forwardDistance;
            position.z += moveForward_.z * c.forwardDistance;
            position.y += c.jumpHeight;
            scale = {
                baseScale_.x * (1.0f - c.resultStretch * 0.36f),
                baseScale_.y * (1.0f + c.resultStretch),
                baseScale_.z * (1.0f - c.resultStretch * 0.36f)
            };
            rotation = resultRotation_;
        }

        posePosition_ = position;
        currentScale_ = scale;
        player_->SetTranslate(position);
        player_->SetRotation(rotation);
        player_->SetScale(scale);
        player_->UpdateLocalMatrix();
        player_->UpdateWorldMatrix();
    }

    void SetTuning(const Tuning& tuning) { tuning_ = tuning; }
    const Tuning& GetTuning() const { return tuning_; }
    Tuning& EditTuning() { return tuning_; }
    bool IsActive() const { return isActive_; }
    bool WasControlActive() const { return savedControlActive_; }
    const Vector3& GetBasePosition() const { return basePosition_; }
    const Vector3& GetBaseScale() const { return baseScale_; }
    const Vector3& GetBaseRotation() const { return baseRotation_; }
    const Vector3& GetPosePosition() const { return posePosition_; }
    const Vector3& GetCurrentScale() const { return currentScale_; }
    const Vector3& GetMoveForward() const { return moveForward_; }
    const Vector3& GetMoveRight() const { return moveRight_; }

private:
    static constexpr float kPi = 3.1415926535f;
    static constexpr float kTwoPi = kPi * 2.0f;

    static float NormalizeYaw(float yaw) {
        while (yaw > kPi) yaw -= kTwoPi;
        while (yaw < -kPi) yaw += kTwoPi;
        return yaw;
    }

    static Vector3 DirectionFromYaw(float yaw) {
        return { std::sin(yaw), 0.0f, std::cos(yaw) };
    }

    static Vector3 NormalizePlanarDirection(const Vector3& direction, const Vector3& fallback) {
        Vector3 result = { direction.x, 0.0f, direction.z };
        const float length = std::sqrt(result.x * result.x + result.z * result.z);
        if (length <= 0.0001f) {
            return fallback;
        }
        return { result.x / length, 0.0f, result.z / length };
    }

    static Vector3 RightFromForward(const Vector3& forward) {
        return { forward.z, 0.0f, -forward.x };
    }

private:
    Player* player_ = nullptr;
    Tuning tuning_{};
    bool isActive_ = false;
    bool savedControlActive_ = true;
    Vector3 basePosition_{};
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 baseRotation_{};
    Vector3 resultRotation_{};
    Vector3 posePosition_{};
    Vector3 currentScale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 moveForward_ = { 0.0f, 0.0f, 1.0f };
    Vector3 moveRight_ = { 1.0f, 0.0f, 0.0f };
};

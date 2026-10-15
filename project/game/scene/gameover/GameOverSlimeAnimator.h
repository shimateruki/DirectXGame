#pragma once
#include "Object3d.h"
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cmath>

// 倒れ姿勢 → 起き上がり → 画面右への退場を一つの状態機械で管理する。
class GameOverSlimeAnimator {
  public:
    // 倒れたスライムの基準姿勢と床の高さを記録し、演出を最初から始めます。
    void Reset(engine::Object3d* slime) {
        // 現在のモデルAABBから床の高さを取って、形状の違うスライムも接地させる。
        timer_ = exitTimer_ = 0.0f;
        phase_ = Phase::Downed;
        hasBaseTransform_ = false;
        if (!slime)
            return;
        basePosition_ = slime->GetTranslate();
        baseScale_ = slime->GetScale();
        baseRotation_ = slime->GetRotation();
        slime->UpdateWorldMatrix(false);
        floorY_ = slime->GetModelWorldAABB().min.y;
        currentPosition_ = basePosition_;
        currentScale_ = baseScale_;
        currentRotation_ = baseRotation_;
        hasBaseTransform_ = true;
        Update(slime, 0.0f);
    }
    // 倒れ姿勢または退場モーションを進め、床に接する姿勢を反映します。
    void Update(engine::Object3d* slime, float deltaTime) {
        if (!slime || !hasBaseTransform_)
            return;
        const float dt = std::isfinite(deltaTime) ? (std::max)(deltaTime, 0.0f) : 0.0f;
        timer_ += dt;
        if (phase_ != Phase::Downed) {
            UpdateExitRight(slime, dt);
            return;
        }
        const float breathe = std::sin(timer_ * 1.7f);
        currentPosition_ = basePosition_;
        // 顔(-Z)を床へ伏せ、床の法線になるローカルZを潰す。
        // 上下反転だけでは開いた目が見えるため、倒れ姿勢はうつ伏せにする。
        currentScale_ = {baseScale_.x * (1.32f - breathe * 0.003f), baseScale_.y * 1.08f,
                         baseScale_.z * (0.22f + breathe * 0.003f)};
        currentRotation_ = {-kPi * 0.5f, baseRotation_.y, 0.0f};
        ApplyGroundedPose(slime, 0.0f);
    }
    // 倒れ状態から起き上がり、水平の指定方向へ退場する動きを開始します。
    // 方向が無効なら +X、距離が無効なら 10 を採用します。
    void StartExitRight(engine::Object3d* slime, const engine::Vector3& direction, float distance) {
        // 退場方向は水平面へ投影し、ゼロ方向だけ固定の右方向へ置き換える。
        if (phase_ != Phase::Downed)
            return;
        if (!slime || !hasBaseTransform_) {
            phase_ = Phase::ExitFinished;
            return;
        }
        exitTimer_ = 0.0f;
        exitStartScale_ = currentScale_;
        exitStartRotation_ = currentRotation_;
        exitDirection_ = {direction.x, 0.0f, direction.z};
        const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        if (std::isfinite(length) && length > 0.001f) {
            exitDirection_.x /= length;
            exitDirection_.z /= length;
        } else
            exitDirection_ = {1.0f, 0.0f, 0.0f};
        exitDistance_ = std::isfinite(distance) ? (std::max)(distance, 0.0f) : 10.0f;
        phase_ = Phase::ExitRight;
    }
    bool IsExiting() const { return phase_ != Phase::Downed; }
    bool IsExitAnimationFinished() const { return phase_ == Phase::ExitFinished; }
    float GetDizzyAlpha() const {
        return phase_ == Phase::Downed ? 0.70f : 0.70f * (1.0f - Clamp01(exitTimer_ / 0.22f));
    }
    engine::Vector3 GetDizzyAnchorWorld() const { return dizzyAnchor_; }
    float GetTimer() const { return timer_; }
    static constexpr float GetImpactTime() { return 0.0f; }
    bool HasBaseTransform() const { return hasBaseTransform_; }

  private:
    enum class Phase { Downed, ExitRight, ExitFinished };
    static constexpr float kPi = 3.14159265f;
    static float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
    static float Smooth(float v) {
        v = Clamp01(v);
        return v * v * (3.0f - 2.0f * v);
    }
    // 現在の変形を反映し、モデル境界の最下点を床面と跳ね高さに合わせます。
    void ApplyGroundedPose(engine::Object3d* slime, float hop) {
        slime->SetScale(currentScale_);
        slime->SetRotation(currentRotation_);
        slime->SetTranslate(currentPosition_);
        slime->UpdateWorldMatrix(false);
        // 回転中もモデル境界の最下点を床へ合わせ、中心点基準の浮き沈みを防ぐ。
        currentPosition_.y += floorY_ + hop - slime->GetModelWorldAABB().min.y;
        slime->SetTranslate(currentPosition_);
        slime->SetEnableLighting(false);
        slime->SetBlendMode(BlendMode::kNormal);
        slime->SetColor({0.88f, 0.97f, 1.0f, 1.0f});
        slime->SetEmissive(1.0f);
        slime->UpdateWorldMatrix(false);
        const AABB bounds = slime->GetModelWorldAABB();
        dizzyAnchor_ = {(bounds.min.x + bounds.max.x) * 0.5f, bounds.max.y + 0.14f,
                        (bounds.min.z + bounds.max.z) * 0.5f};
        slime->RefreshRenderCameraData();
    }
    // 起き上がり、進行方向への旋回、加速移動の順に退場姿勢を更新します。
    void UpdateExitRight(engine::Object3d* slime, float dt) {
        if (phase_ == Phase::ExitFinished)
            return;
        exitTimer_ += dt;
        constexpr float standDuration = 0.80f;
        constexpr float turnDuration = 0.22f;
        constexpr float runDuration = 1.35f;
        float hop = 0.0f;
        currentPosition_ = basePosition_;
        if (exitTimer_ < standDuration) {
            const float u = Smooth(exitTimer_ / standDuration);
            currentRotation_ = engine::Math::Lerp(exitStartRotation_, baseRotation_, u);
            currentScale_ = engine::Math::Lerp(exitStartScale_, baseScale_, u);
            hop = std::sin(u * kPi) * 0.08f;
        } else {
            const float turn = Smooth((exitTimer_ - standDuration) / turnDuration);
            // スライムモデルの正面は-Z。通常プレイヤーと同じ180度補正で進行方向を向く。
            const float yaw = std::atan2(exitDirection_.x, exitDirection_.z) + kPi;
            const float yawDelta = std::atan2(std::sin(yaw - baseRotation_.y), std::cos(yaw - baseRotation_.y));
            currentRotation_ = {baseRotation_.x, baseRotation_.y + yawDelta * turn, baseRotation_.z};
            currentScale_ = baseScale_;
            const float u = Clamp01((exitTimer_ - standDuration - turnDuration) / runDuration);
            // 加速して画面外まで進む。起き上がり完了前に横移動を始めない。
            const float move = u * u;
            currentPosition_ = basePosition_ + exitDirection_ * (exitDistance_ * move);
            const float bounce = std::sin(u * kPi * 4.0f);
            hop = std::abs(bounce) * 0.10f;
            currentScale_.x *= 1.0f - std::abs(bounce) * 0.035f;
            currentScale_.y *= 1.0f + std::abs(bounce) * 0.06f;
        }
        ApplyGroundedPose(slime, hop);
        if (exitTimer_ >= standDuration + turnDuration + runDuration)
            phase_ = Phase::ExitFinished;
    }
    engine::Vector3 basePosition_{}, baseScale_{1, 1, 1}, baseRotation_{};
    engine::Vector3 currentPosition_{}, currentScale_{1, 1, 1}, currentRotation_{}, dizzyAnchor_{};
    engine::Vector3 exitStartScale_{1, 1, 1}, exitStartRotation_{}, exitDirection_{1, 0, 0};
    float floorY_ = 0.0f, exitDistance_ = 10.0f, timer_ = 0.0f, exitTimer_ = 0.0f;
    bool hasBaseTransform_ = false;
    Phase phase_ = Phase::Downed;
};

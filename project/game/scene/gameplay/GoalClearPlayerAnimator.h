#pragma once

#include "Player.h"
#include "engine/utility/math/AnimationInterpolation.h"

#include <algorithm>
#include <cmath>

// 王冠取得時のプレイヤー勝利モーションを管理します。
// 位置と拡縮は滑らかな5次補間、回転はクォータニオン補間で接続します。
class GoalClearPlayerAnimator {
  public:
    // 王冠取得から勝利着地までの演出時刻と姿勢調整値。Time 系は演出開始からの秒。
    struct Tuning {
        float crownLandTime = 2.28f;
        float anticipationStartTime = 2.48f;
        float jumpStartTime = 2.72f;
        float apexTime = 3.08f;
        float resultUiTime = 3.10f;
        float victoryLandTime = 3.46f;
        float readyTime = 5.55f;
        float jumpHeight = 2.20f;
        float forwardDistance = 0.35f;
        float anticipationDepth = 0.28f;
        float landingSquash = 0.26f;
        float anticipationSquash = 0.42f;
        float takeoffStretch = 0.45f;
        float resultStretch = 0.12f;
        float resultYawBias = 0.12f;
    };

    // プレイヤーの操作権と開始姿勢を保存し、王冠または指定方向へ向けて勝利演出を開始します。
    // player がない場合は開始せず false を返します。
    bool Start(Player* player, const engine::Vector3& crownPosition, const engine::Vector3& preferredForward = engine::Vector3{}) {
        player_ = player;
        if (!player_) {
            isActive_ = false;
            return false;
        }

        // 元の操作権・姿勢を保存し、ムービー中はこの基準から毎フレーム姿勢を計算します。
        savedControlActive_ = player_->IsControlActive();
        basePosition_ = player_->GetTranslate();
        baseScale_ = player_->GetScale();
        baseRotation_ = player_->GetRotation();
        posePosition_ = basePosition_;
        currentScale_ = baseScale_;

        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});

        // 指定された前方がない場合は王冠へ向きます。Visual Yaw の差を含めてモデル正面を合わせます。
        const engine::Vector3 crownForward =
            NormalizePlanarDirection({crownPosition.x - basePosition_.x, 0.0f, crownPosition.z - basePosition_.z},
                                     DirectionFromYaw(baseRotation_.y - player_->GetVisualYawOffset()));
        const engine::Vector3 desiredForward = NormalizePlanarDirection(preferredForward, crownForward);
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

    // 保持している演出状態を初期化します。プレイヤーの姿勢は復元しません。
    void Reset() {
        player_ = nullptr;
        isActive_ = false;
        savedControlActive_ = true;
        basePosition_ = {};
        baseScale_ = {1.0f, 1.0f, 1.0f};
        baseRotation_ = {};
        resultRotation_ = {};
        posePosition_ = basePosition_;
        currentScale_ = baseScale_;
        moveForward_ = {0.0f, 0.0f, 1.0f};
        moveRight_ = {1.0f, 0.0f, 0.0f};
    }

    // 中断時に保存済みの開始姿勢をモデルへ戻し、ワールド行列まで更新します。
    void RestoreInitialPose() {
        if (!player_ || !isActive_) {
            return;
        }
        // 演出を中断したときは開始姿勢と行列を戻し、次のScene更新へ古い変形を残しません。
        player_->SetTranslate(basePosition_);
        player_->SetScale(baseScale_);
        player_->SetRotation(baseRotation_);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
        player_->UpdateLocalMatrix();
        player_->UpdateWorldMatrix();
    }

    // 演出開始前の操作可能状態を戻します。
    void RestoreControl() {
        // 演出前に操作不能だった状態も尊重し、無条件に操作を有効にはしません。
        if (player_) {
            player_->SetIsControlActive(savedControlActive_);
        }
    }

    // 演出開始からの絶対時刻で姿勢を評価します。シークしても前フレームには依存しません。
    void Update(float time) {
        if (!player_ || !isActive_) {
            return;
        }

        // 絶対時刻からポーズを計算し、シークやプレビューで前のフレームに依存しないようにします。
        const Tuning& c = tuning_;
        engine::Vector3 position = basePosition_;
        engine::Vector3 scale = baseScale_;
        engine::Vector3 rotation = baseRotation_;

        if (time < c.crownLandTime) {
            // 王冠へ注目している間も、ごく弱い呼吸で静止画に見えないようにします。
            const float breathe = std::sin(time * 3.8f) * 0.5f + 0.5f;
            scale.x = baseScale_.x * (1.0f + breathe * 0.012f);
            scale.y = baseScale_.y * (1.0f - breathe * 0.009f);
            scale.z = baseScale_.z * (1.0f + breathe * 0.012f);
        } else if (time < c.anticipationStartTime) {
            // 王冠が落ちた衝撃だけを短く見せ、次の踏み切り姿勢へつなぎます。
            const float p = AnimationInterpolation::SegmentRate(time, c.crownLandTime, c.anticipationStartTime);
            const float impact = std::sin(p * kPi) * (1.0f - p * 0.45f);
            position.y -= c.landingSquash * 0.10f * impact;
            scale.x = baseScale_.x * (1.0f + c.landingSquash * impact);
            scale.y = baseScale_.y * (1.0f - c.landingSquash * 0.78f * impact);
            scale.z = baseScale_.z * (1.0f + c.landingSquash * impact);
        } else if (time < c.jumpStartTime) {
            // 跳躍前は沈み込みと前傾を同時に増やし、離陸の予備動作にします。
            const float p = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(time, c.anticipationStartTime, c.jumpStartTime),
                AnimationInterpolation::EasingType::SmootherStep);
            position.y -= c.anticipationDepth * p;
            scale.x = baseScale_.x * (1.0f + c.anticipationSquash * p);
            scale.y = baseScale_.y * (1.0f - c.anticipationSquash * 0.92f * p);
            scale.z = baseScale_.z * (1.0f + c.anticipationSquash * p);
            rotation.x += moveForward_.z * 0.070f * p;
            rotation.z -= moveForward_.x * 0.070f * p;
        } else if (time < c.apexTime) {
            // 上昇中は王冠へ前進し、横方向の弧を加えて正面から立体的に見せます。
            const float raw = AnimationInterpolation::SegmentRate(time, c.jumpStartTime, c.apexTime);
            const float travel =
                AnimationInterpolation::ApplyEasing(raw, AnimationInterpolation::EasingType::SmootherStep);
            position.x += moveForward_.x * c.forwardDistance * travel;
            position.z += moveForward_.z * c.forwardDistance * travel;
            const float sideArc = std::sin(raw * kPi * 0.5f) * c.forwardDistance * 0.18f;
            position.x += moveRight_.x * sideArc;
            position.z += moveRight_.z * sideArc;
            position.y += AnimationInterpolation::Lerp(-c.anticipationDepth, c.jumpHeight, travel);

            engine::Vector3 squashScale = {baseScale_.x * (1.0f + c.anticipationSquash),
                                   baseScale_.y * (1.0f - c.anticipationSquash * 0.92f),
                                   baseScale_.z * (1.0f + c.anticipationSquash)};
            engine::Vector3 stretchScale = {baseScale_.x * (1.0f - c.takeoffStretch * 0.48f),
                                    baseScale_.y * (1.0f + c.takeoffStretch),
                                    baseScale_.z * (1.0f - c.takeoffStretch * 0.48f)};
            engine::Vector3 resultScale = {baseScale_.x * (1.0f - c.resultStretch * 0.36f),
                                   baseScale_.y * (1.0f + c.resultStretch),
                                   baseScale_.z * (1.0f - c.resultStretch * 0.36f)};

            // 離陸直後は潰れから縦伸びへ、残りは結果画面用の姿勢へ収束させます。
            if (raw < 0.24f) {
                const float stretchRate =
                    AnimationInterpolation::ApplyEasing(raw / 0.24f, AnimationInterpolation::EasingType::EaseOut);
                scale = AnimationInterpolation::Lerp(squashScale, stretchScale, stretchRate);
            } else {
                const float settleRate = AnimationInterpolation::ApplyEasing(
                    (raw - 0.24f) / 0.76f, AnimationInterpolation::EasingType::SmootherStep);
                scale = AnimationInterpolation::Lerp(stretchScale, resultScale, settleRate);
            }

            // 回転の本体はQuaternion補間とし、踏み切りと空中の傾きだけ後から足します。
            rotation = AnimationInterpolation::SlerpEuler(baseRotation_, resultRotation_, travel);
            const float takeoffLean = (1.0f - travel) * 0.070f;
            rotation.x += moveForward_.z * takeoffLean;
            rotation.z -= moveForward_.x * takeoffLean;
            const float airLean = std::sin(raw * kPi) * 0.075f;
            rotation.x -= moveForward_.z * airLean;
            rotation.z += moveForward_.x * airLean;
            rotation.y = NormalizeYaw(rotation.y + std::sin(raw * kPi) * 0.20f);
        } else if (time < c.victoryLandTime) {
            // 頂点から着地までは高度と横弧を消し、結果姿勢を保ちながら接地で潰します。
            const float raw = AnimationInterpolation::SegmentRate(time, c.apexTime, c.victoryLandTime);
            const float fall =
                AnimationInterpolation::ApplyEasing(raw, AnimationInterpolation::EasingType::SmootherStep);
            position.x += moveForward_.x * c.forwardDistance;
            position.z += moveForward_.z * c.forwardDistance;
            const float sideArc = (1.0f - fall) * c.forwardDistance * 0.18f;
            position.x += moveRight_.x * sideArc;
            position.z += moveRight_.z * sideArc;
            position.y += AnimationInterpolation::Lerp(c.jumpHeight, 0.0f, fall);
            position.y -= c.landingSquash * 0.08f * fall;

            const engine::Vector3 resultScale = {baseScale_.x * (1.0f - c.resultStretch * 0.36f),
                                         baseScale_.y * (1.0f + c.resultStretch),
                                         baseScale_.z * (1.0f - c.resultStretch * 0.36f)};
            const engine::Vector3 landingScale = {baseScale_.x * (1.0f + c.landingSquash * 0.45f),
                                          baseScale_.y * (1.0f - c.landingSquash * 0.40f),
                                          baseScale_.z * (1.0f + c.landingSquash * 0.45f)};
            scale = AnimationInterpolation::Lerp(resultScale, landingScale, fall);
            rotation = resultRotation_;
            const float descentLean = std::sin(raw * kPi) * 0.055f;
            rotation.x += moveForward_.z * descentLean;
            rotation.z -= moveForward_.x * descentLean;
        } else {
            // 結果表示中は一定周期で跳ね続け、左右の傾きを周期ごとに反転します。
            position.x += moveForward_.x * c.forwardDistance;
            position.z += moveForward_.z * c.forwardDistance;
            const float holdTime = time - c.victoryLandTime;
            constexpr float kBounceCycle = 0.68f;
            const float cyclePosition = std::fmod((std::max)(holdTime, 0.0f), kBounceCycle) / kBounceCycle;
            const int cycleIndex = static_cast<int>(std::floor((std::max)(holdTime, 0.0f) / kBounceCycle));
            const float bounceArc = std::sin(cyclePosition * kPi);
            const float bounceHeight = std::clamp(c.jumpHeight * 0.38f, 0.72f, 0.92f);
            position.y += bounceHeight * bounceArc;

            // 接地の潰れと離陸中の伸びを毎周期つなぎ、王冠装着後もスライムらしく跳ね続けます。
            const float contactDistance = (std::min)(cyclePosition, 1.0f - cyclePosition);
            const float contact = std::exp(-contactDistance * 24.0f);
            const float airStretch = std::pow((std::max)(0.0f, bounceArc), 0.72f);
            scale.x = baseScale_.x * (1.0f + c.landingSquash * 0.46f * contact - c.takeoffStretch * 0.10f * airStretch);
            scale.y = baseScale_.y * (1.0f - c.landingSquash * 0.42f * contact + c.takeoffStretch * 0.18f * airStretch);
            scale.z = baseScale_.z * (1.0f + c.landingSquash * 0.46f * contact - c.takeoffStretch * 0.10f * airStretch);
            rotation = resultRotation_;
            const float bounceSide = (cycleIndex % 2 == 0) ? 1.0f : -1.0f;
            const float bounceLean = bounceSide * bounceArc * 0.050f;
            rotation.x += moveRight_.x * bounceLean;
            rotation.z += moveRight_.z * bounceLean;
        }

        // 計算済みポーズをPlayerへまとめて反映し、描画が参照する行列も同フレームで更新します。
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
    const engine::Vector3& GetBasePosition() const { return basePosition_; }
    const engine::Vector3& GetBaseScale() const { return baseScale_; }
    const engine::Vector3& GetBaseRotation() const { return baseRotation_; }
    const engine::Vector3& GetPosePosition() const { return posePosition_; }
    const engine::Vector3& GetCurrentScale() const { return currentScale_; }
    const engine::Vector3& GetMoveForward() const { return moveForward_; }
    const engine::Vector3& GetMoveRight() const { return moveRight_; }

  private:
    static constexpr float kPi = 3.1415926535f;
    static constexpr float kTwoPi = kPi * 2.0f;

    static float NormalizeYaw(float yaw) {
        while (yaw > kPi)
            yaw -= kTwoPi;
        while (yaw < -kPi)
            yaw += kTwoPi;
        return yaw;
    }

    static engine::Vector3 DirectionFromYaw(float yaw) { return {std::sin(yaw), 0.0f, std::cos(yaw)}; }

    static engine::Vector3 NormalizePlanarDirection(const engine::Vector3& direction, const engine::Vector3& fallback) {
        engine::Vector3 result = {direction.x, 0.0f, direction.z};
        const float length = std::sqrt(result.x * result.x + result.z * result.z);
        if (length <= 0.0001f) {
            return fallback;
        }
        return {result.x / length, 0.0f, result.z / length};
    }

    static engine::Vector3 RightFromForward(const engine::Vector3& forward) { return {forward.z, 0.0f, -forward.x}; }

  private:
    Player* player_ = nullptr;
    Tuning tuning_{};
    bool isActive_ = false;
    bool savedControlActive_ = true;
    engine::Vector3 basePosition_{};
    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 baseRotation_{};
    engine::Vector3 resultRotation_{};
    engine::Vector3 posePosition_{};
    engine::Vector3 currentScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 moveForward_ = {0.0f, 0.0f, 1.0f};
    engine::Vector3 moveRight_ = {1.0f, 0.0f, 0.0f};
};

#pragma once

#include "CollisionConfig.h"
#include "Easing.h"
#include <numbers>
#include "CollisionManager.h"
#include "engine/utility/math/Math.h"

#include <algorithm>
#include <cmath>

// ボスの通常移動だけを扱う。攻撃の跳躍や再配置とは状態を共有しない。
// 関連：各ボスがFrameを読み、実際の座標・速度・見た目へ反映する。
// 主な入口：Reset / Begin / Advance
class BossGroundLocomotion {
  public:
    enum class Phase { None, Windup, Travel, Landing };

    // 時間は秒、速度はワールド単位/秒、高さと段差許容値はワールド距離。
    struct Style {
        float windup = 0.15f;
        float travelSpeed = 7.0f;
        float minimumTravelTime = 0.30f;
        float maximumTravelTime = 0.60f;
        float landing = 0.20f;
        float cooldown = 0.70f;
        float arcHeight = 1.0f;
        float maximumHeightDifference = 1.4f;
    };

    // scripted 時だけ position を採用する。takeoff／landed は段階が切り替わったフレームの通知。
    struct Frame {
        engine::Vector3 position{};
        bool scripted = false;
        bool airborne = false;
        bool takeoff = false;
        bool landed = false;
    };

    // 進行中の移動と再使用待ちを破棄する。Actor の座標そのものは戻さない。
    void Reset() {
        phase_ = Phase::None;
        timer_ = 0.0f;
        duration_ = 0.0f;
        cooldown_ = 0.0f;
        leap_ = false;
    }

    // dt秒だけ次の通常移動までの待ち時間を減らす。攻撃の再使用時間とは別。
    void TickCooldown(float deltaTime) { cooldown_ = (std::max)(0.0f, cooldown_ - (std::max)(deltaTime, 0.0f)); }
    // 現在の待ち時間を短縮せず、指定秒数以上になるよう延長する。
    void Delay(float seconds) { cooldown_ = (std::max)(cooldown_, seconds); }
    bool IsActive() const { return phase_ != Phase::None; }
    // 進行中の移動がなく、通常移動の待ち時間も終わっているかを返す。
    bool CanBegin() const { return !IsActive() && cooldown_ <= 0.0f; }
    bool IsLeap() const { return leap_; }
    Phase GetPhase() const { return phase_; }
    float GetProgress() const { return std::clamp(timer_ / (std::max)(duration_, 0.001f), 0.0f, 1.0f); }
    const engine::Vector3& GetDirection() const { return direction_; }

    // 出発・中間・到達地点の地面を確認して開始する。不安全なら距離を半分にして一度再試行する。
    bool Begin(const engine::Vector3& current, const engine::Vector3& desired, bool leap,
               const engine::Object3D* owner, const Style& style) {
        if (!CanBegin()) return false;

        engine::Vector3 displacement = desired - current;
        displacement.y = 0.0f;
        const float distance = std::sqrt(displacement.x * displacement.x + displacement.z * displacement.z);
        if (distance < 0.25f) return false;

        PhysicsQueryFilter filter;
        filter.mask = CollisionCategories::Get("ground");
        filter.ignoredObject = const_cast<engine::Object3D*>(owner);
        const auto groundAt = [&](const engine::Vector3& position) {
            return CollisionManager::GetInstance()->Raycast(position + engine::Vector3{0.0f, 5.0f, 0.0f},
                                                            {0.0f, -1.0f, 0.0f}, 14.0f, filter);
        };

        // この部品を持つボス自身は地面探索から除き、急斜面や高さの不連続を避ける。
        const RaycastHit startHit = groundAt(current);
        if (!startHit.isHit || startHit.normal.y < 0.55f) return false;

        engine::Vector3 candidate = desired;
        RaycastHit endHit;
        bool safeRoute = false;
        for (int attempt = 0; attempt < 2; ++attempt) {
            endHit = groundAt(candidate);
            const RaycastHit middleHit = groundAt((current + candidate) * 0.5f);
            safeRoute = endHit.isHit && middleHit.isHit && endHit.normal.y >= 0.55f &&
                        middleHit.normal.y >= 0.55f &&
                        std::abs(endHit.hitPoint.y - startHit.hitPoint.y) <= style.maximumHeightDifference &&
                        std::abs(middleHit.hitPoint.y - startHit.hitPoint.y) <= style.maximumHeightDifference;
            if (safeRoute) break;
            candidate = current + displacement * 0.5f;
        }
        if (!safeRoute) return false;

        engine::Vector3 actualDisplacement = candidate - current;
        actualDisplacement.y = 0.0f;
        const float actualDistance = std::sqrt(actualDisplacement.x * actualDisplacement.x +
                                               actualDisplacement.z * actualDisplacement.z);
        if (actualDistance < 0.25f) return false;

        // 安全な到達地点を固定し、距離と速度から移動秒数を上下限付きで決める。
        start_ = current;
        end_ = {candidate.x, current.y + endHit.hitPoint.y - startHit.hitPoint.y, candidate.z};
        direction_ = actualDisplacement * (1.0f / actualDistance);
        leap_ = leap;
        style_ = style;
        const float travelDistance = std::sqrt((end_.x - start_.x) * (end_.x - start_.x) +
                                               (end_.z - start_.z) * (end_.z - start_.z));
        travelDuration_ = std::clamp(travelDistance / (std::max)(style.travelSpeed, 0.01f),
                                     style.minimumTravelTime, style.maximumTravelTime);
        phase_ = Phase::Windup;
        timer_ = 0.0f;
        duration_ = style.windup;
        return true;
    }

    // 経過秒数で予備動作→移動→着地を進める。再使用待ちの減算は TickCooldown 側で行う。
    Frame Advance(float deltaTime) {
        Frame frame;
        if (!IsActive()) return frame;

        frame.scripted = true;
        frame.position = start_;
        timer_ += (std::max)(deltaTime, 0.0f);
        if (phase_ == Phase::Windup) {
            if (timer_ >= duration_) {
                phase_ = Phase::Travel;
                timer_ = 0.0f;
                duration_ = travelDuration_;
                frame.takeoff = leap_;
            }
            frame.airborne = leap_ && phase_ == Phase::Travel;
            return frame;
        }

        // 水平補間へ正弦波の高さを足して跳躍を作り、終点では高さを確定させる。
        if (phase_ == Phase::Travel) {
            const float progress = GetProgress();
            const float eased = Easing::SmoothStep(progress);
            frame.position = start_ + (end_ - start_) * eased;
            if (leap_) frame.position.y += std::sin(progress * std::numbers::pi_v<float>) * style_.arcHeight;
            frame.airborne = leap_;
            if (progress >= 1.0f) {
                frame.position = end_;
                frame.airborne = false;
                frame.landed = leap_;
                phase_ = Phase::Landing;
                timer_ = 0.0f;
                duration_ = style_.landing;
                cooldown_ = style_.cooldown;
            }
            return frame;
        }

        frame.position = end_;
        if (timer_ >= duration_) {
            phase_ = Phase::None;
            timer_ = 0.0f;
        }
        return frame;
    }

  private:
    Phase phase_ = Phase::None;
    Style style_{};
    engine::Vector3 start_{};
    engine::Vector3 end_{};
    engine::Vector3 direction_{};
    float timer_ = 0.0f;
    float duration_ = 0.0f;
    float travelDuration_ = 0.0f;
    float cooldown_ = 0.0f;
    bool leap_ = false;
};

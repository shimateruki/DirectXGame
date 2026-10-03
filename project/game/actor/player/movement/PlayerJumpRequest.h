#pragma once
#include <algorithm>

// 入力の記憶とジャンプ権だけを管理し、速度やアニメーションは変更しません。
class PlayerJumpRequest {
public:
    void Reset() { *this = PlayerJumpRequest{}; }
    void Advance(float dt, bool pressed, bool triggered, bool grounded, bool pink) {
        buffer_ = (std::max)(0.0f, buffer_ - dt);
        grace_ = (std::max)(0.0f, grace_ - dt);
        if (!grounded) leftGround_ = true;
        if (grounded && (!spent_ || leftGround_)) {
            spent_ = false;
            leftGround_ = false;
            grace_ = 0.10f;
        }
        if (pink != pink_) {
            buffer_ = charge_ = pendingCharge_ = 0.0f;
            charging_ = false;
        }
        pink_ = pink;
        if (!pink) {
            if (triggered) buffer_ = 0.15f;
            return;
        }
        if (triggered) { charging_ = true; charge_ = 0.0f; buffer_ = 0.0f; }
        if (charging_ && pressed) charge_ = (std::min)(1.0f, charge_ + dt);
        if (charging_ && !pressed) {
            pendingCharge_ = charge_;
            buffer_ = 0.15f;
            charging_ = false;
            charge_ = 0.0f;
        }
    }
    bool Consume(float& charge) {
        if (buffer_ <= 0.0f || grace_ <= 0.0f || spent_) return false;
        charge = pink_ ? pendingCharge_ : 0.0f;
        buffer_ = grace_ = 0.0f;
        spent_ = true;
        leftGround_ = false;
        return true;
    }
    bool IsCharging() const { return charging_; }
    float Charge() const { return charge_; }
    float BufferRemaining() const { return buffer_; }
    float GraceRemaining() const { return grace_; }
private:
    float buffer_ = 0.0f, grace_ = 0.0f, charge_ = 0.0f, pendingCharge_ = 0.0f;
    bool spent_ = false, leftGround_ = false, pink_ = false, charging_ = false;
};

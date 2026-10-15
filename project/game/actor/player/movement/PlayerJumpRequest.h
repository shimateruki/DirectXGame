#pragma once
#include <algorithm>

// 入力の記憶とジャンプ権だけを管理し、速度やアニメーションは変更しません。
class PlayerJumpRequest {
  public:
    // 入力バッファ、接地猶予、溜め量をまとめて初期状態へ戻します。
    void Reset() { *this = PlayerJumpRequest{}; }

    /// <summary>入力状態と接地状態から、ジャンプ要求を一フレーム進めます。</summary>
    /// <param name="dt">前回更新からの経過秒数。負値は 0 として扱う。</param>
    /// <param name="pressed">ジャンプを押し続けているか</param>
    /// <param name="triggered">このフレームで押し始めたか</param>
    /// <param name="grounded">このフレームの接地状態</param>
    /// <param name="pink">溜めジャンプが使えるコピー状態か</param>
    void Advance(float dt, bool pressed, bool triggered, bool grounded, bool pink) {
        // 入力バッファと接地猶予は別々に減らし、着地時だけジャンプ権を回復する。
        const float elapsed = (std::max)(0.0f, dt);
        buffer_ = (std::max)(0.0f, buffer_ - elapsed);
        grace_ = (std::max)(0.0f, grace_ - elapsed);
        if (!grounded)
            leftGround_ = true;
        if (grounded && (!spent_ || leftGround_)) {
            spent_ = false;
            leftGround_ = false;
            grace_ = kGroundGraceSeconds;
        }
        if (pink != pink_) {
            // コピー状態の切替で旧能力の溜め入力を次のジャンプへ持ち越さない。
            buffer_ = charge_ = pendingCharge_ = 0.0f;
            charging_ = false;
        }
        pink_ = pink;
        if (!pink) {
            if (triggered)
                buffer_ = kInputBufferSeconds;
            return;
        }
        if (triggered) {
            charging_ = true;
            charge_ = 0.0f;
            buffer_ = 0.0f;
        }
        if (charging_ && pressed)
            charge_ = (std::min)(kMaxChargeSeconds, charge_ + elapsed);
        if (charging_ && !pressed) {
            // ピンク時は押し始めではなく離した時点で溜め量と発動要求を確定する。
            pendingCharge_ = charge_;
            buffer_ = kInputBufferSeconds;
            charging_ = false;
            charge_ = 0.0f;
        }
    }
    /// <summary>入力と接地猶予が残っていれば一度だけ要求を取り出します。</summary>
    /// <param name="charge">成功時、コピー用の溜め時間を秒で受け取る。失敗時は変更しない。</param>
    bool Consume(float& charge) {
        if (buffer_ <= 0.0f || grace_ <= 0.0f || spent_)
            return false;
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
    static constexpr float kGroundGraceSeconds = 0.10f;
    static constexpr float kInputBufferSeconds = 0.15f;
    static constexpr float kMaxChargeSeconds = 1.0f;

    // 接地猶予と先行入力は独立したタイマーで、Consume 成功時に両方を消費します。
    float buffer_ = 0.0f, grace_ = 0.0f, charge_ = 0.0f, pendingCharge_ = 0.0f;
    bool spent_ = false, leftGround_ = false, pink_ = false, charging_ = false;
};

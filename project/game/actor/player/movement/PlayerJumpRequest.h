#pragma once
#include <algorithm>

// 入力の記憶とジャンプ権だけを管理し、速度やアニメーションは変更しません。
// 関連：PlayerMoverが入力・接地を渡し、受理した要求に従って実際の跳躍速度を設定する。
// 主な入口：Reset / Advance / Consume
class PlayerJumpRequest {
  public:
    // 入力バッファ、接地猶予、溜め量をまとめて初期状態へ戻します。
    void Reset() { *this = PlayerJumpRequest{}; }

    /// <summary>入力状態と接地状態から、ジャンプ要求を一フレーム進めます。</summary>
    /// <param name="deltaTime">前回更新からの経過秒数。負値は 0 として扱う。</param>
    /// <param name="isJumpHeld">ジャンプを押し続けているか</param>
    /// <param name="isJumpTriggered">このフレームで押し始めたか</param>
    /// <param name="isGrounded">このフレームの接地状態</param>
    /// <param name="canChargeJump">溜めジャンプが使えるコピー状態か</param>
    void Advance(float deltaTime, bool isJumpHeld, bool isJumpTriggered, bool isGrounded, bool canChargeJump) {
        // 入力バッファと接地猶予は別々に減らし、着地時だけジャンプ権を回復する。
        const float elapsed = (std::max)(0.0f, deltaTime);
        inputBufferRemainingSeconds_ = (std::max)(0.0f, inputBufferRemainingSeconds_ - elapsed);
        groundGraceRemainingSeconds_ = (std::max)(0.0f, groundGraceRemainingSeconds_ - elapsed);
        if (!isGrounded)
            hasLeftGround_ = true;
        if (isGrounded && (!jumpConsumed_ || hasLeftGround_)) {
            jumpConsumed_ = false;
            hasLeftGround_ = false;
            groundGraceRemainingSeconds_ = kGroundGraceSeconds;
        }
        if (canChargeJump != canChargeJump_) {
            // コピー状態の切替で旧能力の溜め入力を次のジャンプへ持ち越さない。
            inputBufferRemainingSeconds_ = chargeSeconds_ = pendingChargeSeconds_ = 0.0f;
            isCharging_ = false;
        }
        canChargeJump_ = canChargeJump;
        if (!canChargeJump) {
            if (isJumpTriggered)
                inputBufferRemainingSeconds_ = kInputBufferSeconds;
            return;
        }
        if (isJumpTriggered) {
            isCharging_ = true;
            chargeSeconds_ = 0.0f;
            inputBufferRemainingSeconds_ = 0.0f;
        }
        if (isCharging_ && isJumpHeld)
            chargeSeconds_ = (std::min)(kMaxChargeSeconds, chargeSeconds_ + elapsed);
        if (isCharging_ && !isJumpHeld) {
            // ピンク時は押し始めではなく離した時点で溜め量と発動要求を確定する。
            pendingChargeSeconds_ = chargeSeconds_;
            inputBufferRemainingSeconds_ = kInputBufferSeconds;
            isCharging_ = false;
            chargeSeconds_ = 0.0f;
        }
    }

    /// <summary>入力と接地猶予が残っていれば一度だけ要求を取り出します。</summary>
    /// <param name="charge">成功時、コピー用の溜め時間を秒で受け取る。失敗時は変更しない。</param>
    bool Consume(float& charge) {
        if (inputBufferRemainingSeconds_ <= 0.0f || groundGraceRemainingSeconds_ <= 0.0f || jumpConsumed_)
            return false;
        charge = canChargeJump_ ? pendingChargeSeconds_ : 0.0f;
        inputBufferRemainingSeconds_ = groundGraceRemainingSeconds_ = 0.0f;
        jumpConsumed_ = true;
        hasLeftGround_ = false;
        return true;
    }

    bool IsCharging() const { return isCharging_; }

    float Charge() const { return chargeSeconds_; }

    float BufferRemaining() const { return inputBufferRemainingSeconds_; }

    float GraceRemaining() const { return groundGraceRemainingSeconds_; }

  private:
    static constexpr float kGroundGraceSeconds = 0.10f;
    static constexpr float kInputBufferSeconds = 0.15f;
    static constexpr float kMaxChargeSeconds = 1.0f;

    // 接地猶予と先行入力は独立したタイマーで、Consume 成功時に両方を消費します。
    float inputBufferRemainingSeconds_ = 0.0f, groundGraceRemainingSeconds_ = 0.0f, chargeSeconds_ = 0.0f, pendingChargeSeconds_ = 0.0f;
    bool jumpConsumed_ = false, hasLeftGround_ = false, canChargeJump_ = false, isCharging_ = false;
};

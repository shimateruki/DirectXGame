#pragma once
#include "InputManager.h"
#include "Object3d.h"

/// <summary>
/// 速度、重力、接地判定など、物理的に動くキャラクターの基底クラス。
/// </summary>
// Characterは、重力、速度、接地判定、物理衝突補正を持つ移動キャラクターの基底クラスです。
class Character : public Object3d {
public:
    // 衝突時の基本的な押し戻しを行う
        // 衝突相手から必要な物理補正やイベント処理を行います。
bool OnCollision(Object3d* other) override;

    /// <summary>
    /// 速度と重力を反映して座標を更新する。
    /// </summary>
        // 速度、重力、接地状態を反映してTransformを更新します。
void Update(float deltaTime) override;

        // キャラクター本体をライト情報付きで描画します。
void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    Vector3 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& v) { velocity_ = v; }

    bool IsGrounded() const { return isGrounded_; }
    void SetGrounded(bool grounded) { isGrounded_ = grounded; }

    /// <summary>
    /// このキャラクターに適用する重力を設定する。
    /// </summary>
    void SetGravity(float gravity) {
        if (!this->param_.has_value()) {
            this->param_.emplace();
        }
        this->param_->gravity = gravity;
    }

    /// <summary>
    /// 最大落下速度を設定する。
    /// </summary>
    void SetMaxFallSpeed(float maxFallSpeed) {
        if (!this->param_.has_value()) {
            this->param_.emplace();
        }
        this->param_->maxFallSpeed = maxFallSpeed;
    }

        // 衝突情報に基づいて押し戻しや速度の補正を適用します。
void ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute);

    std::unique_ptr<Object3d> Clone() const override;

protected:
    void CaptureReplayCustomState(json& state) const override;
    void RestoreReplayCustomState(const json& state) override;

    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f }; // 現在速度。
    bool isGrounded_ = false;                 // 最後の更新で接地しているか。
};

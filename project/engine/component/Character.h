#pragma once
#include "InputManager.h"
#include "Object3d.h"
#include "CharacterMotor.h"

/// 速度、重力、接地判定、物理衝突補正を持つ移動キャラクターの基底クラスです。
/// 入力やAIは派生クラスで速度へ変換し、このクラスがCharacterMotorへ移動を委譲します。
class Character : public Object3d {
public:
    /// ソリッド属性との衝突をCharacterMotorで解決します。
    bool OnCollision(Object3d* other) override;

    /// 重力と外力を速度へ反映し、CharacterMotorで座標を更新します。
    void Update(float deltaTime) override;

    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    Vector3 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& v) { velocity_ = v; }

    /// 一定時間だけ目標速度を重ねます。ノックバックなど、状態遷移を伴わない外力向けです。
    void ApplyExternalImpulse(const Vector3& velocity, float duration = 0.16f);

    bool IsGrounded() const { return isGrounded_; }
    void SetGrounded(bool grounded) { isGrounded_ = grounded; }
    CharacterMotor& GetCharacterMotor() { return characterMotor_; }
    const CharacterMotor& GetCharacterMotor() const { return characterMotor_; }

    /// このキャラクターへ適用する重力加速度を設定します。
    void SetGravity(float gravity) {
        if (!this->param_.has_value()) {
            this->param_.emplace();
        }
        this->param_->gravity = gravity;
    }

    /// 下向き速度の上限を設定します。
    void SetMaxFallSpeed(float maxFallSpeed) {
        if (!this->param_.has_value()) {
            this->param_.emplace();
        }
        this->param_->maxFallSpeed = maxFallSpeed;
    }

    /// 衝突情報に基づいて押し戻しと速度補正を適用します。
    void ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute);

    std::unique_ptr<Object3d> Clone() const override;

protected:
    void CaptureReplayCustomState(json& state) const override;
    void SyncCharacterMotorSettings();
    void RestoreReplayCustomState(const json& state) override;

    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f };
    bool isGrounded_ = false;
    Vector3 externalImpulseVelocity_ = { 0.0f, 0.0f, 0.0f };
    float externalImpulseTimer_ = 0.0f;
    float externalImpulseDuration_ = 0.0f;
    CharacterMotor characterMotor_;
    bool externalImpulseVerticalPending_ = false;
};

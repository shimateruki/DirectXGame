#pragma once
#include "Object3d.h"
#include "InputManager.h"

/// <summary>
/// 物理的な振る舞いを持つキャラクターの基底クラス
/// </summary>
class Character : public Object3d {
public:


    // 衝突応答の基本処理（押し戻し）を実装
    bool OnCollision(Object3d* other) override;

    /// <summary>
    /// 物理挙動を適用した更新処理
    /// </summary>
    void Update(float deltaTime) override;

    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    bool IsGrounded() const { return isGrounded_; }
    /// <summary>
     /// このキャラクターに適用される重力を設定する
     /// </summary>
    void SetGravity(float gravity) {
        // 安全策：もしパラメータがまだ無ければ作る
        if (!this->param_.has_value()) {
            this->param_.emplace();
        }
        this->param_->gravity = gravity;
    }

    /// <summary>
    /// このキャラクターの最大落下速度を設定する
    /// </summary>
    void SetMaxFallSpeed(float maxFallSpeed) {
        // 安全策
        if (!this->param_.has_value()) {
            this->param_.emplace();
        }
        this->param_->maxFallSpeed = maxFallSpeed;
    }



    void ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute);

    void SetVelocity(const Vector3& v) { velocity_ = v; }
    const Vector3& GetVelocity() const { return velocity_; }

    std::unique_ptr<Object3d> Clone() const override;
protected:
    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f }; // 速度

    bool isGrounded_ = false;

};
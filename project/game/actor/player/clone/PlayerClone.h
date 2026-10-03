#pragma once
#include "Character.h"

/// <summary>
/// プレイヤーが投げる分身オブジェクト
/// </summary>
class PlayerClone : public Character {
public:
    void Initialize(Object3dCommon* common, const Vector3& position, const Vector3& velocity);
    void Update(float deltaTime) override;

    bool IsGrounded() const { return isGrounded_; }
    bool IsExpired() const { return lifetimeTimer_ <= 0.0f; }
    void Destroy() { lifetimeTimer_ = -1.0f; }

private:
    float lifetimeTimer_ = 3.0f; // 3秒で消滅
};

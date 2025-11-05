#pragma once
#include "engine/3d/Object3d.h"
#include "engine/io/InputManager.h"

/// <summary>
/// 物理的な振る舞いを持つキャラクターの基底クラス
/// </summary>
class Character : public Object3d {
public:


    // 衝突応答の基本処理（押し戻し）を実装
    bool OnCollision(Object3d* other) override;

    // ▼▼▼ 以下を追加 ▼▼▼
    /// <summary>
    /// 物理挙動を適用した更新処理
    /// </summary>
    void Update() override;

    bool IsGrounded() const { return isGrounded_; }
    /// <summary>
    /// このキャラクターに適用される重力を設定する
    /// </summary>
    void SetGravity(float gravity) { gravity_ = gravity; }

    /// <summary>
    /// このキャラクターの最大落下速度を設定する
    /// </summary>
    void SetMaxFallSpeed(float maxFallSpeed) { maxFallSpeed_ = maxFallSpeed; }

    std::unique_ptr<Object3d> Clone() const override;
protected:
    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f }; // 速度
    float gravity_ = 0.015f;
    float maxFallSpeed_ = 1.0f;
    bool isGrounded_ = false;

};
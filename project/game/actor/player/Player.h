#pragma once
#include "Character.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "PlayerMover.h"
#include "IAnimationState.h" 
#include <memory>
#include <string>

// 前方宣言
class IMoveStrategy;

class Player : public Character {
public:
    void Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem);
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    bool OnCollision(Object3d* other) override;

    // --- State Machine (アニメーション制御) ---
    void ChangeState(std::unique_ptr<IAnimationState> newState);
    void PlayAnimation(const std::string& animName, bool loop = true);

    // --- Mover (移動制御) ---
    void SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy);

    // --- Getters / Setters ---
    Vector3 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& v) { velocity_ = v; }

    Vector3 GetRotation() const { return transform_.rotate; }
    void SetRotation(const Vector3& r) { transform_.rotate = r; }
    void SetRotationY(float y) { transform_.rotate.y = y; }

    void SetLockOn(bool isLockingOn) { isLockingOn_ = isLockingOn; }
    bool IsLockingOn() const { return isLockingOn_; }

    void SetIsControlActive(bool isActive) { isControlActive_ = isActive; }

    InputManager* GetInputManager() { return inputManager_; }

    float GetJumpPower() const {
        if (param_.has_value()) return param_->jumpPower;
        return 10.0f;
    }

    float GetMoveSpeed() const {
        if (param_.has_value()) return param_->speed;
        return 0.5f;
    }

private:
    // コンポーネント
    std::unique_ptr<PlayerMover> mover_ = nullptr;
    std::unique_ptr<IAnimationState> state_ = nullptr; // 現在のアニメーション状態

    // 依存オブジェクト
    InputManager* inputManager_ = nullptr;
    ParticleSystem* particleSystem_ = nullptr;

    // フラグ
    bool isLockingOn_ = false;
    bool isControlActive_ = true;
};
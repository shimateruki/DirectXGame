#pragma once
#include "Character.h"
#include "InputManager.h"
#include "ParticleSystem.h"
#include "PlayerMover.h"
#include "IAnimationState.h" 
#include <memory>
#include <string>

class IMoveStrategy; // 前方宣言

// プレイヤーキャラクターの統合制御クラス
class Player : public Character {
public:
    // ==================================================
    // 基本サイクル
    // ==================================================
    void Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem);
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    // ==================================================
    // 衝突判定
    // ==================================================
    bool OnCollision(Object3d* other) override;

    // ==================================================
    // アニメーション・状態管理 (State Pattern)
    // ==================================================
    void ChangeState(std::unique_ptr<IAnimationState> newState);
    void PlayAnimation(const std::string& animName, bool loop = true);

    // ==================================================
    // 移動制御 (Strategy Pattern)
    // ==================================================
    void SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy);

    // ==================================================
    // アクセッサ
    // ==================================================
    // --- 物理・姿勢 ---
    Vector3 GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& v) { velocity_ = v; }

    Vector3 GetRotation() const { return transform_.rotate; }
    void SetRotation(const Vector3& r) { transform_.rotate = r; }
    void SetRotationY(float y) { transform_.rotate.y = y; }

    // --- 状態フラグ ---
    void SetLockOn(bool isLockingOn) { isLockingOn_ = isLockingOn; }
    bool IsLockingOn() const { return isLockingOn_; }
    void SetIsControlActive(bool isActive) { isControlActive_ = isActive; }

    // --- 各種パラメータ取得 ---
    InputManager* GetInputManager() { return inputManager_; }

    float GetJumpPower() const {
        return param_.has_value() ? param_->jumpPower : 10.0f;
    }

    float GetMoveSpeed() const {
        return param_.has_value() ? param_->speed : 0.5f;
    }

    // ==================================================
    // 無敵フレーム (Invincibility) 管理
    // ==================================================
    void SetInvincible(bool inv);
    bool IsInvincible() const { return isInvincible_; }

private:
    // --- 内部コンポーネント ---
    std::unique_ptr<PlayerMover> mover_ = nullptr;            // 移動処理の委譲先
    std::unique_ptr<IAnimationState> state_ = nullptr;        // 現在のアクション状態

    // --- 外部システム参照 ---
    InputManager* inputManager_ = nullptr;
    ParticleSystem* particleSystem_ = nullptr;

    // --- プレイヤー状態フラグ ---
    bool isLockingOn_ = false;       // 敵をロックオンしているか
    bool isControlActive_ = true;    // 入力を受け付ける状態か（デモシーン等で制限用）

    // --- 無敵関連 ---
    bool isInvincible_ = false;
    Vector4 savedColor_ = {1.0f, 1.0f, 1.0f, 1.0f}; // 無敵解除時に戻す色
};
#pragma once

#include "Character.h"
#include "InputManager.h"
#include "engine/utility/math/Math.h"

class ParticleSystem;
class SpriteCommon;

/// 新規ゲームの開始時に使える、移動と衝突だけを持つ基本Playerです。
/// WASD、ジャンプ、カメラ基準移動を初期実装として残し、ゲーム固有能力は持ちません。
class Player : public Character {
public:
    Player() = default;
    ~Player() override;

    void Initialize(
        Object3dCommon* common,
        InputManager* inputManager,
        ParticleSystem* particleSystem = nullptr,
        SpriteCommon* spriteCommon = nullptr);
    void Update(float deltaTime) override;
    std::unique_ptr<Object3d> Clone() const override;

    void SetIsControlActive(bool active) { isControlActive_ = active; }
    bool IsControlActive() const { return isControlActive_; }

    void SetRespawnPosition(const Vector3& position) { respawnPosition_ = position; }
    Vector3 GetRespawnPosition() const { return respawnPosition_; }

    void SetMoveSpeed(float speed) { moveSpeed_ = (std::max)(0.0f, speed); }
    float GetMoveSpeed() const { return moveSpeed_; }
    void SetJumpPower(float power) { jumpPower_ = (std::max)(0.0f, power); }
    float GetJumpPower() const { return jumpPower_; }

    Vector3 GetRotation() const { return transform_.rotate; }
    void SetRotation(const Vector3& rotation) {
        transform_.rotate = rotation;
        transform_.quaternion = Math::EulerToQuaternion(rotation);
        transform_.isQuaternionMaster = true;
    }
    void SetRotationY(float yaw) {
        Vector3 rotation = GetRotation();
        rotation.y = yaw;
        SetRotation(rotation);
    }

private:
    Vector3 CalculateMoveDirection() const;
    void UpdateFacing(const Vector3& direction, float deltaTime);

    InputManager* inputManager_ = nullptr;
    bool isControlActive_ = true;
    float moveSpeed_ = 8.0f;
    float jumpPower_ = 10.0f;
    float turnResponse_ = 12.0f;
    Vector3 respawnPosition_{}; // 将来の復帰処理から利用する初期位置です。
};

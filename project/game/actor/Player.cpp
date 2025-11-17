#include "Player.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include <string> 
#include "EventManager.h"
#include "CameraManager.h"
void Player::Initialize(Object3dCommon* common, InputManager* inputManager) {
    Object3d::Initialize(common);
    inputManager_ = inputManager;
    SetColliderType(ColliderType::kAABB);
    SetCollisionSize({ 1.0f, 1.0f, 1.0f });
}

void Player::Update(float deltaTime) {
    static Math math;

    // --- 1. 移動戦略（ストラテジー）による移動ベクトルの計算 ---
    if (moveStrategy_) {
        // 戦略クラスに移動ベクトルを計算してもらう
        Vector3 move = moveStrategy_->CalculateVelocity(this);

        // 計算結果を速度に反映 (X と Z のみ)
        velocity_.x = move.x;
        velocity_.z = move.z;
    }

    // --- 2. 回転処理 ---
    if (!isLockingOn_) {
        // (ロックオン中でない場合のみ、移動方向を向く)
        Camera* camera = CameraManager::GetInstance()->GetMainCamera();
        Camera::FollowMode cameraMode = camera ? camera->GetFollowMode() : Camera::FollowMode::kFixed;

        if (cameraMode == Camera::FollowMode::kFirstPerson) {
            transform_.rotate.y = camera->GetRotation().y;
        }
        // XZ平面での速度ベクトルの長さで判断
        else if (math.Length(Vector3{ velocity_.x, 0.0f, velocity_.z }) > 0.001f) {
            transform_.rotate.y = std::atan2(velocity_.x, velocity_.z);
        }
    }

    // --- 3. ジャンプ処理 ---
    if (isGrounded_ && inputManager_->IsKeyTriggered(DIK_SPACE)) {
        const float kJumpVelocity = 30.0f; 
        velocity_.y = kJumpVelocity;
    }

    // --- 4. 親(Character)のUpdateを呼ぶ ---
    Character::Update(deltaTime);
}

bool Player::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();

    // ★ 1. まず Player 側で CollisionInfo を取得
    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) {
        return false; // 当たってないなら即終了
    }

    // --- 2. 物理処理 (地面) ---
    if (attribute & kAllSolid) {
        // ★ 親の物理処理関数を呼ぶ (押し戻し・接地判定)
        ApplyPhysicsCollision(info, attribute);
    }

    // --- 3. イベント発行 ---
    if (attribute & (kEnemy)) { 

        // ★ イベントに法線(info.normal)を詰めて発行する
        EventManager::GetInstance()->Dispatch(PlayerHitEvent{ other, info.normal });
    }

    return true; // (当たった)
}

/// <summary>
/// 描画処理
/// </summary>
void Player::Draw() {

    Character::Draw();
}
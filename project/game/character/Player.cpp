#include "Player.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/base/Math.h"
#include <string> 
#include "EventManager.h" 

void Player::Initialize(Object3dCommon* common, InputManager* inputManager) {
    Object3d::Initialize(common);
    inputManager_ = inputManager;
    SetColliderType(ColliderType::kAABB);
    SetCollisionSize({ 1.0f, 1.0f, 1.0f });
}
//
void Player::Update() {

// --- キー入力による移動処理 ---
    Vector3 move = { 0.0f, 0.0f, 0.0f };
    const float moveSpeed = 0.1f;
    if (inputManager_->IsKeyPressed(DIK_W)) { move.z += moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_S)) { move.z -= moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_A)) { move.x -= moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_D)) { move.x += moveSpeed; }
    
    // ★ 1. 水平方向の移動は velocity_ に直接代入する
    velocity_.x = move.x;
    velocity_.z = move.z;

    // ★ 2. ジャンプ処理 
    if (isGrounded_ && inputManager_->IsKeyTriggered(DIK_SPACE)) {
        const float kJumpVelocity = 0.5f; // ジャンプの初速 
        velocity_.y = kJumpVelocity;      // Y軸の速度に初速を与える
    }


    Character::Update();
 
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
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
    Vector3 move = { 0.0f, 0.0f, 0.0f };
    const float moveSpeed = 6.0f;

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    if (camera && inputManager_) {

        if (isLockingOn_) {
            // --- (A) ロックオン中の移動 (ストラフ) ---

            // プレイヤーの現在の向き（敵の方向）を基準にする
            Vector3 playerForward = { 0, 0, 1 }; // Z+ が前
            Vector3 playerRight = { 1, 0, 0 };   // X+ が右

            // (Object3d.h に GetRotation() を追加した前提)
            Matrix4x4 rotateMat = math.MakeRotateYMatrix(transform_.rotate.y);
            playerForward = math.TransformNormal(playerForward, rotateMat);
            playerRight = math.TransformNormal(playerRight, rotateMat);

            // WASD入力で前後左右に平行移動
            if (inputManager_->IsKeyPressed(DIK_W)) {
                move += playerForward * moveSpeed;
            }
            if (inputManager_->IsKeyPressed(DIK_S)) {
                move += (playerForward * moveSpeed) * -1.0f; 
            }
            if (inputManager_->IsKeyPressed(DIK_A)) {
                move += (playerRight * moveSpeed) * -1.0f;
            }
            if (inputManager_->IsKeyPressed(DIK_D)) {
                move += playerRight * moveSpeed;
            }

        } else {
            // --- (B) 通常時の移動 (元のコード) ---

            Vector3 cameraForward = camera->GetTargetPoint() - camera->GetEye();
            Vector3 cameraRight = math.Cross({ 0.0f, 1.0f, 0.0f }, cameraForward);
            cameraForward.y = 0.0f;
            cameraRight.y = 0.0f;

            // (ゼロベクトルチェック)
            if (math.Length(cameraForward) > 0.001f) {
                cameraForward = math.Normalize(cameraForward);
            }
            if (math.Length(cameraRight) > 0.001f) {
                cameraRight = math.Normalize(cameraRight);
            }

            if (inputManager_->IsKeyPressed(DIK_W)) {
                move += cameraForward * moveSpeed;
            }
            if (inputManager_->IsKeyPressed(DIK_S)) {
                move += (cameraForward * moveSpeed) * -1.0f;
            }
            if (inputManager_->IsKeyPressed(DIK_A)) {
                move += (cameraRight * moveSpeed) * -1.0f;
            }
            if (inputManager_->IsKeyPressed(DIK_D)) {
                move += cameraRight * moveSpeed;
            }
        }


        // --- 速度に反映 ---
        velocity_.x = move.x;
        velocity_.z = move.z;

        // ★ 3. 回転処理 
        if (!isLockingOn_) {
            Camera::FollowMode cameraMode = camera->GetFollowMode();

            if (cameraMode == Camera::FollowMode::kFirstPerson) {
                transform_.rotate.y = camera->GetRotation().y;
            } else if (math.Length(move) > 0.001f) {
                transform_.rotate.y = std::atan2(move.x, move.z);
            }
        }



        // 4. ジャンプ処理 
        if (isGrounded_ && inputManager_->IsKeyTriggered(DIK_SPACE)) {
            const float kJumpVelocity = 30.0f;
            velocity_.y = kJumpVelocity;
        }
    }

    // --- 5. 親(Character)のUpdateを呼ぶ ---
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
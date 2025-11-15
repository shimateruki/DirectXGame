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
//
void Player::Update() {
    static Math math;
    Vector3 move = { 0.0f, 0.0f, 0.0f };
    const float moveSpeed = 0.1f;

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    if (camera && inputManager_) { // カメラと入力がある場合のみ

        // (1) カメラの前方・右方ベクトルを取得
        Vector3 cameraForward = camera->GetTargetPoint() - camera->GetEye();
        Vector3 cameraRight = math.Cross({ 0.0f, 1.0f, 0.0f }, cameraForward);

        // (2) Y軸を潰して正規化 (地面と平行にする)
        cameraForward.y = 0.0f;
        cameraRight.y = 0.0f;

        // ゼロベクトルでないことを確認してから正規化
        if (math.Length(cameraForward) > 0.001f) {
            cameraForward = math.Normalize(cameraForward);
        }
        if (math.Length(cameraRight) > 0.001f) {
            cameraRight = math.Normalize(cameraRight);
        }

        // (3) 入力から移動ベクトルを計算
        if (inputManager_->IsKeyPressed(DIK_W)) {
            move += cameraForward * moveSpeed;
        }
        if (inputManager_->IsKeyPressed(DIK_S)) {
            move += (cameraForward * -moveSpeed);
        }
        if (inputManager_->IsKeyPressed(DIK_A)) {
            move += (cameraRight * -moveSpeed); // Aは左
        }
        if (inputManager_->IsKeyPressed(DIK_D)) {
            move += cameraRight * moveSpeed; 
        }
    }
    

    // ★ 2. 水平方向の移動は velocity_ に直接代入する
    velocity_.x = move.x;
    velocity_.z = move.z;

    // --- 3. プレイヤーの「向き」の更新 ---
    Camera::FollowMode cameraMode = camera ? camera->GetFollowMode() : Camera::FollowMode::kFixed;

    // (1人称視点の場合)
    if (cameraMode == Camera::FollowMode::kFirstPerson) {
        // カメラのY軸回転をプレイヤーのY軸回転にコピー
        transform_.rotate.y = camera->GetRotation().y;
    }
    // (3人称視点などで、移動入力があった場合)
    else if (math.Length(move) > 0.001f) {
        // 移動方向（moveベクトル）を向く
        transform_.rotate.y = std::atan2(move.x, move.z);
    }
    // (※ 移動入力がない時は、向きをそのまま維持する)

    // ★ 4. ジャンプ処理 (元のコード)
    if (isGrounded_ && inputManager_->IsKeyTriggered(DIK_SPACE)) {
        const float kJumpVelocity = 0.5f; // ジャンプの初速 
        velocity_.y = kJumpVelocity;      // Y軸の速度に初速を与える
    }

    // --- 5. 親(Character)のUpdateを呼ぶ ---
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
#include "Player.h"
#include "engine/3d/Model.h"
#include "engine/3d/CollisionConfig.h"
#include "engine/base/Math.h"
#include <string> // ★ OutputDebugStringA のために追加

void Player::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
    inputManager_ = InputManager::GetInstance();
    SetColliderType(ColliderType::kAABB);
    SetCollisionSize({ 0.5f, 1.0f, 0.5f });
}

void Player::Update() {
    // --- キー入力による移動処理 ---
    Vector3 move = { 0.0f, 0.0f, 0.0f };
    const float moveSpeed = 0.1f;
    if (inputManager_->IsKeyPressed(DIK_W)) { move.z += moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_S)) { move.z -= moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_A)) { move.x -= moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_D)) { move.x += moveSpeed; }
    transform_.translate += move;
    Object3d::Update();
}

void Player::OnCollision(Object3d* other) {
    // 1. まず親クラスの物理応答処理(地形との押し戻し)を呼び出す
    Character::OnCollision(other);

    uint32_t attribute = other->GetCollisionAttribute();

    // 2. Player固有の処理 (敵)
    if (attribute & kEnemy) {
        if (model_ && model_->GetMaterial()) {
            model_->GetMaterial()->color = { 1.0f, 0.0f, 0.0f, 1.0f };
        }
    }

    // ▼▼▼ 3. デバッグログ用の処理 ▼▼▼
    if (attribute & kAllGround) {
        // 押し戻しは親が実行済みだが、方向を知るために再度判定する
        CollisionInfo collision;
        collision.isColliding = false;

        // プレイヤー(自分)の形状タイプに合わせて判定を呼び出す
        if (this->GetColliderType() == ColliderType::kAABB) {
            collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
        } else if (this->GetColliderType() == ColliderType::kSphere) {
            if (other->GetColliderType() == ColliderType::kAABB) {
                collision = CheckSphereAABBCollision(this->GetWorldPosition(), this->GetCollisionRadius(), other->GetAABB());
            } else if (other->GetColliderType() == ColliderType::kSphere) {
                collision = CheckSphereCollision(this->GetWorldPosition(), this->GetCollisionRadius(), other->GetWorldPosition(), other->GetCollisionRadius());
            }
        }

        // 衝突方向のログをVisual Studioの「出力」ウィンドウに表示
        if (collision.isColliding) {
            if (collision.normal.y == 1.0f) {
                OutputDebugStringA("Hit From Bottom (Landed)\n");
            } else if (collision.normal.y == -1.0f) {
                OutputDebugStringA("Hit From Top (Head)\n");
            } else if (collision.normal.x == 1.0f) {
                OutputDebugStringA("Hit From Left\n");
            } else if (collision.normal.x == -1.0f) {
                OutputDebugStringA("Hit From Right\n");
            } else if (collision.normal.z == 1.0f) {
                OutputDebugStringA("Hit From Back\n");
            } else if (collision.normal.z == -1.0f) {
                OutputDebugStringA("Hit From Front\n");
            }
        }
    }
}
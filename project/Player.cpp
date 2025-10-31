#include "Player.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/base/Math.h"
#include <string> // ★ OutputDebugStringA のために追加

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
    transform_.translate += move;
 
}

// ★ 戻り値を bool に変更済みのはず
bool Player::OnCollision(Object3d* other) {
    bool hitGround = false;
    uint32_t attribute = other->GetCollisionAttribute();

    if (attribute & kAllGround) {
        hitGround = Character::OnCollision(other);
    }

    if (attribute & kEnemy)
    {
        OutputDebugStringA("Hit");
    }

    return hitGround;
}

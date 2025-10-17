#include "Player.h"
#include "engine/3d/Model.h"
#include "engine/3d/CollisionConfig.h"
#include "engine/base/Math.h"

void Player::Initialize(Object3dCommon* common)
{
    Object3d::Initialize(common);
    inputManager_ = InputManager::GetInstance();
}

void Player::Update()
{
    // --- キー入力による移動処理 ---
    Vector3 move = { 0.0f, 0.0f, 0.0f };
    const float moveSpeed = 0.1f;
    if (inputManager_->IsKeyPressed(DIK_W)) { move.z += moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_S)) { move.z -= moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_A)) { move.x -= moveSpeed; }
    if (inputManager_->IsKeyPressed(DIK_D)) { move.x += moveSpeed; }
    transform_.translate += move;

    // 親の更新処理を呼び出す
    Object3d::Update();
}

void Player::OnCollision(Object3d* other) {
    // 1. まず親クラスの物理応答処理(押し戻し)を呼び出す
    Character::OnCollision(other);

    // 2. その後で、Player固有の処理を記述する
    if (other->GetCollisionAttribute() & kEnemy) {
        if (model_ && model_->GetMaterial()) {
            model_->GetMaterial()->color = { 1.0f, 0.0f, 0.0f, 1.0f };
        }
    }
}
#include "Player.h"
#include "engine/3d/Model.h" // マテリアル操作のため

void Player::OnCollision(Object3d* other) {
    // もし相手が敵(kEnemy)なら、自分の色を赤に変える
    if (other->GetCollisionAttribute() == kEnemy) {
        if (model_ && model_->GetMaterial()) {
            model_->GetMaterial()->color = { 1.0f, 0.0f, 0.0f, 1.0f }; // 赤色
            OutputDebugStringA("EnemyHit");
        }
    }

}
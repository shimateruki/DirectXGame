#define NOMINMAX
#include "Character.h"
#include "engine/3d/CollisionConfig.h"
#include "engine/base/Math.h"
#include <algorithm> // std::min, std::max
#include <cmath>     // std::abs

// ★ Math のインスタンスを作成
static Math math;
static const float kGravity = 0.015f;
static const float kMaxFallSpeed = 1.0f;

void Character::Update() {

    // ★ 1. 更新の最初に、接地フラグを「false」にリセット
    isGrounded_ = false;

    // ★ 2. 重力（Y軸マイナスの加速度）を速度に加算
    velocity_.y -= kGravity;

    // ★ (お好みで) 落下速度が速くなりすぎないように制限（クランプ）
    if (velocity_.y < -kMaxFallSpeed) {
        velocity_.y = -kMaxFallSpeed;
    }

    // ★ 3. 速度を座標に反映 
    transform_.translate += velocity_;
}


bool Character::OnCollision(Object3d* other) {
    // 相手が地形(kAllGround)でなければ、物理応答はしない
    if (!(other->GetCollisionAttribute() & kAllGround)) {
        return false;
    }

    // --- 1. 衝突判定の実行 ---
    ColliderType myType = this->GetColliderType();
    ColliderType otherType = other->GetColliderType();
    CollisionInfo collision;
    collision.isColliding = false; // 初期化

    if (myType == ColliderType::kAABB && otherType == ColliderType::kAABB) {
        collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kSphere) {
        collision = CheckSphereCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(),
            other->GetWorldPosition(), other->GetCollisionRadius());
    } else if (myType == ColliderType::kAABB && otherType == ColliderType::kSphere) {
        collision = CheckSphereAABBCollision(
            other->GetWorldPosition(), other->GetCollisionRadius(), this->GetAABB());
        collision.normal = collision.normal * -1.0f; // 法線を反転
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kAABB) {
        collision = CheckSphereAABBCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(), other->GetAABB());
    }

    // --- 2. 衝突応答 ---
    if (collision.isColliding) {

        // ★ 座標を押し戻す (めり込んだ分だけ座標を補正)
        this->transform_.translate += (collision.normal * collision.penetration);

        //  速度を補正 (壁にめり込む速度成分を打ち消す)
        float dot = math.Dot(velocity_, collision.normal);

        // 速度が法線と逆向き (dot < 0) ＝ めり込もうとしている場合のみ
        if (dot < 0) {
            // 法線方向の速度成分（dot）を、速度ベクトルから差し引く
            velocity_ = velocity_ - (collision.normal * dot);
        }

        //接地判定
        if (collision.normal.y > 0.9f) {
            isGrounded_ = true;
        }
    }

    // 衝突したかどうかを返す
    return collision.isColliding;
}
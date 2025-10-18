#define NOMINMAX
#include "Character.h"
#include "engine/3d/CollisionConfig.h"
#include "engine/base/Math.h"
#include <algorithm> // std::min, std::max
#include <cmath>     // std::abs





// --- Character::OnCollision の実装 ---

void Character::Initialize(Object3dCommon* common)
{
}

void Character::Update()
{
}

void Character::OnCollision(Object3d* other) {
    // 相手が地形(kAllGround)でなければ、物理応答はしない
    if (!(other->GetCollisionAttribute() & kAllGround)) {
        return;
    }

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

    // 衝突していたら、自分を押し戻す
    if (collision.isColliding) {
        transform_.translate += collision.normal * collision.penetration;
    }
}
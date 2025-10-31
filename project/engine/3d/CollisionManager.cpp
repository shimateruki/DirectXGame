#include "CollisionManager.h"
#include "engine/base/Math.h"

CollisionManager* CollisionManager::GetInstance() {
    static CollisionManager instance;
    return &instance;
}

void CollisionManager::AddObject(Object3d* object) {
    objects_.push_back(object);
}

void CollisionManager::ClearObjects() {
    objects_.clear();
}

void CollisionManager::Update() {
    // オブジェクトのリストを総当たりでチェック
    // std::list::iterator を使う
    for (auto itA = objects_.begin(); itA != objects_.end(); ++itA) {
        Object3d* objA = *itA;

        // イテレータをコピーして1つ進める
        auto itB = itA;
        itB++;

        for (; itB != objects_.end(); ++itB) {
            Object3d* objB = *itB;

            // 1. 衝突フィルタリング
            // (AのマスクとBの属性) AND (BのマスクとAの属性) が両方通らなければスキップ
            if (!((objA->GetCollisionMask() & objB->GetCollisionAttribute()) &&
                (objB->GetCollisionMask() & objA->GetCollisionAttribute()))) {
                continue;
            }

            // 2. 形状タイプに応じて衝突判定
            ColliderType typeA = objA->GetColliderType();
            ColliderType typeB = objB->GetColliderType();
            CollisionInfo collisionInfo; // 衝突情報を格納する
            collisionInfo.isColliding = false; // 初期化

            // AABB vs AABB
            if (typeA == ColliderType::kAABB && typeB == ColliderType::kAABB) {
                collisionInfo = CheckAABBCollision(objA->GetAABB(), objB->GetAABB());
            }
            // Sphere vs Sphere
            else if (typeA == ColliderType::kSphere && typeB == ColliderType::kSphere) {
                collisionInfo = CheckSphereCollision(
                    objA->GetWorldPosition(), objA->GetCollisionRadius(),
                    objB->GetWorldPosition(), objB->GetCollisionRadius());
            }
            // Sphere vs AABB
            else if (typeA == ColliderType::kSphere && typeB == ColliderType::kAABB) {
                collisionInfo = CheckSphereAABBCollision(
                    objA->GetWorldPosition(), objA->GetCollisionRadius(), objB->GetAABB());
            }
            // AABB vs Sphere
            else if (typeA == ColliderType::kAABB && typeB == ColliderType::kSphere) {
                collisionInfo = CheckSphereAABBCollision(
                    objB->GetWorldPosition(), objB->GetCollisionRadius(), objA->GetAABB());
                collisionInfo.normal = collisionInfo.normal * -1.0f;
            }


            // 3. 衝突していたら、両方のオブジェクトに通知
            if (collisionInfo.isColliding) {
                // お互いの OnCollision 関数を呼び出す
                objA->OnCollision(objB);
                objB->OnCollision(objA);
            }
        }
    }
}

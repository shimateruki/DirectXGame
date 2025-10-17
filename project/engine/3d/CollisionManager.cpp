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
    for (auto itA = objects_.begin(); itA != objects_.end(); ++itA) {
        Object3d* objA = *itA;

        auto itB = itA;
        itB++;
        for (; itB != objects_.end(); ++itB) {
            Object3d* objB = *itB;

            // 衝突フィルタリング
            // (AのマスクとBの属性) AND (BのマスクとAの属性) が両方通らなければスキップ
            if (!((objA->GetCollisionMask() & objB->GetCollisionAttribute()) &&
                (objB->GetCollisionMask() & objA->GetCollisionAttribute()))) {
                continue;
            }

            // 2点間の距離を計算
            Vector3 posA = objA->GetWorldPosition();
            Vector3 posB = objB->GetWorldPosition();
            float distance = Math().Length(posA - posB);

            // 衝突しているか？ (距離 < 半径の合計)
            if (distance <= objA->GetCollisionRadius() + objB->GetCollisionRadius()) {
                // 衝突した双方の OnCollision 関数を呼び出す
                objA->OnCollision(objB);
                objB->OnCollision(objA);
            }
        }
    }
}
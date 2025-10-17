#include "Character.h"
#include "engine/3d/CollisionConfig.h"
#include "engine/base/Math.h"
#include "engine/3d/Model.h"

void Character::Initialize(Object3dCommon* common) {

}

void Character::Update() {
  

}

void Character::OnCollision(Object3d* other) {
    // 相手が何らかの地形属性を持っていたら、押し戻し処理を行う
    if (other->GetCollisionAttribute() & kAllGround) {
        Vector3 posA = this->GetWorldPosition();
        Vector3 posB = other->GetWorldPosition();
        Vector3 vecAtoB = posB - posA;
        Math math;
        float distance = math.Length(vecAtoB);
        float totalRadius = this->GetCollisionRadius() + other->GetCollisionRadius();
        float penetration = totalRadius - distance;

        if (penetration > 0) {
            Vector3 pushBackDirection = { 0,0,0 };
            if (distance > 0.001f) {
                pushBackDirection = (vecAtoB / distance) * -1.0f;
            } else {
                pushBackDirection = { 1.0f, 0.0f, 0.0f };
            }
            transform_.translate += pushBackDirection * penetration;
        }
    }
}
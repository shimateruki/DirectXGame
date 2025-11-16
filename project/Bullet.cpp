#include "Bullet.h"
#include "CollisionConfig.h" 
#include <EventManager.h>

void Bullet::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
    // 属性は Fire() 側で設定する
}

void Bullet::Fire(const Vector3& position, const Vector3& velocity,
    float life, uint32_t attribute, uint32_t mask) {

    transform_.translate = position;
    velocity_ = velocity;
    lifeTimer_ = life;
    isDead_ = false;


    SetCollisionAttribute(attribute);
    SetCollisionMask(mask);
}

void Bullet::Update(float deltaTime) {
    if (isDead_) { return; }

    lifeTimer_ -= deltaTime; 
    if (lifeTimer_ <= 0.0f) {
        isDead_ = true;
    }
    // 速度（秒速）を経過時間分だけ座標に加算
    transform_.translate += velocity_ * deltaTime;
}

bool Bullet::OnCollision(Object3d* other) {

    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false; 
    }
    // 何かに当たったら死亡
    isDead_ = true;
    EventManager::GetInstance()->Dispatch(BulletHitEvent{ other, this });
    return true;
}
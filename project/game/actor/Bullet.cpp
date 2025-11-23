#include "Bullet.h"
#include "CollisionConfig.h" 
#include <EventManager.h>
#include"ParticleSystem.h"

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
    if (particleSystem_) {
        // 弾の現在位置に「煙」を置く
        particleSystem_->SpawnParticles(
            transform_.translate,
            1,                            // 個数: 毎フレーム1個
            0.0f,                         // 初速: 0（その場に置く）
            nullptr,
            0.0f,
            { 0.8f, 0.8f, 1.0f, 0.5f },   // 色: 薄い青白（半透明）
            { 0.0f, 0.0f, 0.0f, 0.0f },   // 終了色: 透明へ
            0.2f, 0.3f,                   // 寿命: 短め
            0.5f, 0.0f                    // サイズ: 弾より少し小さめから0へ
        );
    }
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
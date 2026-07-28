#include "Bullet.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "GPUParticleManager.h"
#include <EventManager.h>
#include <algorithm>
#include <cmath>

void Bullet::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
    // 属性は Fire() 側で設定する
}

void Bullet::Fire(const Vector3& position, const Vector3& velocity,
    float life, uint32_t attribute, uint32_t mask, float damage,
    const StatusEffectApplication& statusEffect, DamageType damageType) {

    transform_.translate = position;
    velocity_ = velocity;
    lifeTimer_ = life;
    damage_ = (std::max)(0.0f, damage);
    damageType_ = damageType;
    statusEffect_ = statusEffect;
    trailTimer_ = 0.0f;
    impactEmitted_ = false;
    isDead_ = false;


    SetCollisionAttribute(attribute);
    SetCollisionMask(mask);
}

void Bullet::ConfigureVfx(const std::string& trailPreset, const std::string& impactPreset,
    float trailInterval, float trailSpeedScale) {
    trailPreset_ = trailPreset;
    impactPreset_ = impactPreset;
    trailInterval_ = (std::max)(0.01f, trailInterval);
    trailTimer_ = 0.0f;
    trailSpeedScale_ = (std::max)(0.05f, trailSpeedScale);
}

void Bullet::Expire(bool playImpact) {
    if (playImpact) {
        EmitImpactVfx();
    }
    isDead_ = true;
}

void Bullet::Update(float deltaTime) {
    if (isDead_) { return; }

    trailTimer_ -= deltaTime;
    if (trailTimer_ <= 0.0f) {
        EmitTrailVfx();
        trailTimer_ += trailInterval_;
    }

    lifeTimer_ -= deltaTime;
    if (lifeTimer_ <= 0.0f) {
        isDead_ = true;
        return;
    }

    const Vector3 displacement = velocity_ * deltaTime;
    const float travelDistance = Math::Length(displacement);
    if (travelDistance <= 0.000001f) {
        return;
    }

    // 1フレームの移動区間を球で走査し、高速弾が薄い対象を飛び越えないようにします。
    PhysicsQueryFilter filter;
    filter.mask = GetCollisionMask();
    filter.ignoredObject = this;
    filter.ignoreDescendants = true;
    filter.includeTriggers = true;

    const Vector3 direction = displacement / travelDistance;
    const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(
        transform_.translate,
        GetCollisionRadius(),
        direction,
        travelDistance,
        filter);
    if (hit.isHit && hit.hitObject) {
        // SphereCastが重なりを検出した中心へ止め、同フレーム末尾の通常衝突処理へ渡します。
        transform_.translate += direction * hit.distance;
        return;
    }

    transform_.translate += displacement;
}

bool Bullet::OnCollision(Object3d* other) {

    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false;
    }
    // 何かに当たったら死亡
    EmitImpactVfx();
    isDead_ = true;
    EventManager::GetInstance()->Dispatch(BulletHitEvent{ other, this });
    return true;
}

void Bullet::EmitTrailVfx() {
    if (trailPreset_.empty()) {
        return;
    }
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    Vector3 direction = velocity_;
    if (Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 0.0f, 1.0f };
    } else {
        direction = Math::Normalize(direction);
    }
    particles->EmitDirected(trailPreset_, GetTranslate(), direction, trailSpeedScale_);
}

void Bullet::EmitImpactVfx() {
    if (impactEmitted_ || impactPreset_.empty()) {
        return;
    }
    impactEmitted_ = true;
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    Vector3 direction = velocity_;
    if (Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 1.0f, 0.0f };
    } else {
        direction = Math::Normalize(direction);
    }
    particles->EmitDirected(impactPreset_, GetTranslate(), direction, trailSpeedScale_);
}

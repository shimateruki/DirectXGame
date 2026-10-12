#include "Bullet.h"
#include "CameraManager.h"
#include "MeshEffectManager.h"
#include <unordered_set>
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "GPUParticleManager.h"
#include <EventManager.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>

void Bullet::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
    // 属性は Fire() 側で設定する
}

void Bullet::Fire(const Vector3& position, const Vector3& velocity, float life, uint32_t attribute, uint32_t mask,
                  float damage, const StatusEffectApplication& statusEffect, DamageType damageType) {
    // プールから再利用しても前回の爆発半径や演出状態を引き継がないよう、発射単位で初期化する。
    SetTranslate(position);
    velocity_ = velocity;
    gravity_ = 0.0f;
    explosionRadius_ = 0.0f;
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

void Bullet::ConfigureVfx(const std::string& trailPreset, const std::string& trailSecondaryPreset,
                          const std::string& impactPreset, const std::string& impactSecondaryPreset,
                          float trailInterval, float trailSpeedScale, bool trailReverseDirection, bool impactDirected) {
    // 発生間隔と速度倍率を下限で抑え、停止した弾でも演出更新が暴走しないようにする。
    trailPreset_ = trailPreset;
    trailSecondaryPreset_ = trailSecondaryPreset;
    impactPreset_ = impactPreset;
    impactSecondaryPreset_ = impactSecondaryPreset;
    trailInterval_ = (std::max)(0.01f, trailInterval);
    trailTimer_ = 0.0f;
    trailSpeedScale_ = (std::max)(0.05f, trailSpeedScale);
    trailReverseDirection_ = trailReverseDirection;
    impactDirected_ = impactDirected;
}

void Bullet::Expire(bool playImpact) {
    // 寿命切れなど、衝突以外の終了でも必要なら着弾演出を一度だけ出す。
    if (playImpact) {
        EmitImpactVfx();
    }
    isDead_ = true;
}

void Bullet::Update(float deltaTime) {
    if (isDead_) {
        return;
    }

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

    if (magmaHopperPresentation_) {
        // 見た目だけを伸縮し、衝突半径は発射時の値を維持する。
        const float base = GetColliderConfig().size.x * 1.3f;
        const float stretch = 0.92f + 0.40f * std::clamp(std::abs(velocity_.y) / 20.0f, 0.0f, 1.0f);
        const float width = 1.0f / std::sqrt(stretch);
        SetScale({base * width, base * stretch, base * width});
        if (auto* camera = CameraManager::GetInstance()->GetActiveCamera()) {
            const Vector3 towardCamera = camera->GetEye() - GetTranslate();
            if (towardCamera.x * towardCamera.x + towardCamera.z * towardCamera.z > 0.001f)
                SetRotation({0.0f, std::atan2(-towardCamera.x, -towardCamera.z), 0.0f});
        }
    }

    // 放物線のフレーム間変位を計算し、既存のSphereCastで地形との衝突を調べる。
    const Vector3 displacement = velocity_ * deltaTime + Vector3{0.0f, -0.5f * gravity_ * deltaTime * deltaTime, 0.0f};
    velocity_.y -= gravity_ * deltaTime;
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
    const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(GetTranslate(), GetCollisionRadius(),
                                                                       direction, travelDistance, filter);
    if (hit.isHit && hit.hitObject) {
        // SphereCastが重なりを検出した中心へ止め、同フレーム末尾の通常衝突処理へ渡します。
        SetTranslate(GetTranslate() + direction * hit.distance);
        if (explosionRadius_ > 0.0f)
            Explode();
        return;
    }

    SetTranslate(GetTranslate() + displacement);
}

bool Bullet::OnCollision(Object3d* other) {
    // 爆発弾は範囲判定へ移し、通常弾だけ直接命中イベントを送る。
    if (isDead_ || !other)
        return false;

    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false;
    }
    if (explosionRadius_ > 0.0f) {
        Explode();
        return true;
    }
    // 何かに当たったら死亡
    EmitImpactVfx();
    isDead_ = true;
    EventManager::GetInstance()->Dispatch(BulletHitEvent{other, this});
    return true;
}

void Bullet::EmitTrailVfx() {
    // 速度ゼロでも無効な方向ベクトルを GPU パーティクルへ渡さない。
    if (trailPreset_.empty() && trailSecondaryPreset_.empty()) {
        return;
    }
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    Vector3 direction = velocity_;
    if (Math::Length(direction) <= 0.001f) {
        direction = {0.0f, 0.0f, 1.0f};
    } else {
        direction = Math::Normalize(direction);
    }
    if (trailReverseDirection_) {
        direction = direction * -1.0f;
    }
    if (!trailPreset_.empty()) {
        particles->EmitDirected(trailPreset_, GetTranslate(), direction, trailSpeedScale_);
    }
    if (!trailSecondaryPreset_.empty()) {
        particles->EmitDirected(trailSecondaryPreset_, GetTranslate(), direction, trailSpeedScale_);
    }
}

void Bullet::EmitImpactVfx() {
    // 衝突と強制終了が重なっても着弾演出を重複発生させない。
    if (impactEmitted_ || (impactPreset_.empty() && impactSecondaryPreset_.empty())) {
        return;
    }
    impactEmitted_ = true;
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }
    Vector3 direction = velocity_;
    if (Math::Length(direction) <= 0.001f) {
        direction = {0.0f, 1.0f, 0.0f};
    } else {
        direction = Math::Normalize(direction);
    }
    for (const std::string* preset : {&impactPreset_, &impactSecondaryPreset_}) {
        if (preset->empty()) {
            continue;
        }
        if (impactDirected_) {
            particles->EmitDirected(*preset, GetTranslate(), direction, trailSpeedScale_);
        } else {
            particles->Emit(*preset, GetTranslate());
        }
    }
}

void Bullet::Explode() {
    if (isDead_)
        return;
    // 直撃と爆風が同じフレームに二重加算されないよう、先に弾を無効化する。
    isDead_ = true;
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    const Vector3 center = GetTranslate();
    if (auto* effects = MeshEffectManager::GetInstance()) {
        const float core = explosionRadius_ / 3.0f;
        const float ring = explosionRadius_ / 5.8f;
        effects->SpawnEffectAt("Resources/json/effect/effect_bomb_core_flash.json", center, {0, 0, 0},
                               {core, core, core});
        PhysicsQueryFilter groundFilter;
        groundFilter.mask = CollisionCategories::Get("ground");
        groundFilter.ignoredObject = this;
        const auto ground =
            CollisionManager::GetInstance()->Raycast(center, {0, -1, 0}, explosionRadius_ + 1.0f, groundFilter);
        const Vector3 ringPosition = ground.isHit ? ground.hitPoint + Vector3{0, 0.06f, 0} : center;
        effects->SpawnEffectAt("Resources/json/effect/effect_bomb_shockwave_ring.json", ringPosition, {0, 0, 0},
                               {ring, ring, ring});
    }
    if (auto* particles = GPUParticleManager::GetInstance(); particles && particles->IsInitialized()) {
        particles->Emit("hit_bomb_fire_core", center);
        particles->Emit("hit_bomb_fire_sparks", center);
        particles->Emit("hit_bomb_orange_smoke", center);
    }
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("player");
    filter.ignoredObject = this;
    std::unordered_set<Object3d*> damaged;
    for (const auto& hit : CollisionManager::GetInstance()->OverlapSphere(center, explosionRadius_, filter)) {
        if (!hit.object || !damaged.insert(hit.object).second)
            continue;
        Vector3 delta = hit.object->GetWorldPosition() - center;
        PhysicsQueryFilter wallFilter;
        wallFilter.mask = CollisionCategories::Get("ground");
        wallFilter.ignoredObject = this;
        const float distance = Math::Length(delta);
        if (distance > 0.05f) {
            const auto wall = CollisionManager::GetInstance()->Raycast(center, delta / distance, distance, wallFilter);
            if (wall.isHit && wall.distance > 0.05f && wall.distance < distance - 0.1f)
                continue;
        }
        DamageEvent event;
        event.target = hit.object;
        event.attacker = this;
        event.damageAmount = damage_;
        event.damageType = DamageType::Explosion;
        EventManager::GetInstance()->Dispatch(event);
    }
}

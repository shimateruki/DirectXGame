#include "Bullet.h"
#include "CameraManager.h"
#include "MeshEffectManager.h"
#include "DebrisEffectManager.h"
#include <unordered_set>
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "GPUParticleManager.h"
#include <game/system/rules/EventManager.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>

void Bullet::Initialize(engine::Object3dCommon* common) {
    engine::Object3d::Initialize(common);

    // 属性は Fire() 側で設定する
}

void Bullet::Fire(const engine::Vector3& position, const engine::Vector3& velocity, float life, uint32_t attribute, uint32_t mask,
                  float damage, const StatusEffectApplication& statusEffect, DamageType damageType) {
    // プールから再利用しても前回の爆発半径や演出状態を引き継がないよう、発射単位で初期化する。
    SetTranslate(position);
    velocity_ = velocity;
    impactNormal_ = {0.0f, 0.0f, 0.0f};
    gravity_ = 0.0f;
    explosionRadius_ = 0.0f;
    lifeTimer_ = life;
    damage_ = (std::max)(0.0f, damage);
    damageType_ = damageType;
    statusEffect_ = statusEffect;
    trailTimer_ = 0.0f;
    burningTimer_ = 0.0f;
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

void Bullet::ConfigureBurningVfx(const std::string& preset, float radius) {
    burningPreset_ = preset;
    burningRadius_ = radius;
    burningTimer_ = 0.0f;
}

void Bullet::ConfigureDestructionVfx(const std::string& debrisPreset, const std::string& secondaryDebrisPreset,
                                     float debrisScale, bool impactOnExpiry, bool destroyOnSolidCastHit) {
    impactDebrisPreset_ = debrisPreset;
    impactDebrisSecondaryPreset_ = secondaryDebrisPreset;
    impactDebrisScale_ = (std::isfinite(debrisScale) && debrisScale > 0.0f) ? debrisScale : 1.0f;
    impactOnExpiry_ = impactOnExpiry;
    destroyOnSolidCastHit_ = destroyOnSolidCastHit;
}

void Bullet::Expire(bool playImpact) {
    if (isDead_) {
        return;
    }
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

    if (!previewOnly_ && trailTimer_ <= 0.0f) {
        EmitTrailVfx();
        trailTimer_ += trailInterval_;
    }

    if (!burningPreset_.empty()) {
        burningTimer_ -= deltaTime;
        if (burningTimer_ <= 0.0f) {
            EmitBurningVfx();
            burningTimer_ += previewOnly_ ? 0.05f : 0.03f;
        }
    }

    lifeTimer_ -= deltaTime;

    if (lifeTimer_ <= 0.0f) {
        Expire(impactOnExpiry_);
        return;
    }

    if (magmaHopperPresentation_) {
        // 見た目だけを伸縮し、衝突半径は発射時の値を維持する。
        const float base = GetColliderConfig().size.x * 1.3f;
        const float stretch = 0.92f + 0.40f * std::clamp(std::abs(velocity_.y) / 20.0f, 0.0f, 1.0f);
        const float width = 1.0f / std::sqrt(stretch);
        SetScale({base * width, base * stretch, base * width});

        if (auto* camera = CameraManager::GetInstance()->GetActiveCamera()) {
            const engine::Vector3 towardCamera = camera->GetEye() - GetTranslate();
            if (towardCamera.x * towardCamera.x + towardCamera.z * towardCamera.z > 0.001f)
                SetRotation({0.0f, std::atan2(-towardCamera.x, -towardCamera.z), 0.0f});
        }
    }

    // 放物線のフレーム間変位を計算し、既存のSphereCastで地形との衝突を調べる。
    const engine::Vector3 displacement = velocity_ * deltaTime + engine::Vector3{0.0f, -0.5f * gravity_ * deltaTime * deltaTime, 0.0f};
    velocity_.y -= gravity_ * deltaTime;
    if (previewOnly_) {
        // 編集用の弾は飛翔だけを再現し、衝突とダメージ処理へ渡しません。
        SetTranslate(GetTranslate() + displacement);
        return;
    }
    const float travelDistance = engine::Math::Length(displacement);

    if (travelDistance <= 0.000001f) {
        return;
    }

    // 1フレームの移動区間を球で走査し、高速弾が薄い対象を飛び越えないようにします。
    PhysicsQueryFilter filter;
    filter.mask = GetCollisionMask();
    filter.ignoredObject = this;
    filter.ignoreDescendants = true;
    filter.includeTriggers = !destroyOnSolidCastHit_;
    filter.refineSphereCastHit = true;

    const engine::Vector3 direction = displacement / travelDistance;
    const RaycastHit hit = CollisionManager::GetInstance()->SphereCast(GetTranslate(), GetCollisionRadius(),
                                                                       direction, travelDistance, filter);
    if (hit.isHit && hit.hitObject) {
        // 球の最初の接触点よりわずかに手前へ置き、着弾エフェクトの中心が壁へ潜らないようにします。
        SetTranslate(GetTranslate() + direction * (std::max)(0.0f, hit.distance - 0.002f));
        impactNormal_ = hit.normal;
        if (explosionRadius_ > 0.0f)
            Explode();
        else if (destroyOnSolidCastHit_ &&
                 (hit.hitObject->GetCollisionAttribute() &
                  (CollisionCategories::Get("ground") | CollisionCategories::Get("map_block"))))
            Expire(true);
        return;
    }

    SetTranslate(GetTranslate() + displacement);
}

bool Bullet::OnCollision(engine::Object3d* other) {
    // 爆発弾は範囲判定へ移し、通常弾だけ直接命中イベントを送る。
    if (isDead_ || !other)
        return false;

    CollisionInfo info = CheckCollision(other);

    if (!info.isColliding) {
        return false;
    }

    if (explosionRadius_ > 0.0f) {
        impactNormal_ = info.normal;
        Explode();
        return true;
    }

    // 何かに当たったら死亡
    impactNormal_ = info.normal;
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

    engine::Vector3 direction = velocity_;

    if (engine::Math::Length(direction) <= 0.001f) {
        direction = {0.0f, 0.0f, 1.0f};
    } else {
        direction = engine::Math::Normalize(direction);
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

void Bullet::EmitBurningVfx() {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (!particles || !particles->IsInitialized()) {
        return;
    }

    const auto it = particles->GetPresets().find(burningPreset_);
    if (it == particles->GetPresets().end()) {
        return;
    }

    // 専用プリセットの色と寿命は Editor で調整し、空間サイズだけ弾の半径へ追従させます。
    GPUParticleConfig config = it->second;
    const float radius = (std::max)(0.1f, burningRadius_);
    config.emitPos = GetTranslate();
    config.shapeRadius *= radius;
    config.emitArea = config.emitArea * radius;
    config.baseSize *= radius;
    config.midSize *= radius;
    config.endSize *= radius;
    config.velocityVariance *= radius;
    config.emitCount = static_cast<int>((std::min)(12.0f, 3.0f + std::sqrt(radius) * 2.0f));

    // 炎を進行方向の後ろへなびかせ、弾を単なる発光球に見せないようにします。
    const float speed = engine::Math::Length(config.emitVelocity) * std::sqrt(radius);
    if (engine::Math::Length(velocity_) > 0.001f) {
        config.emitVelocity = engine::Math::Normalize(velocity_) * -speed;
    }
    particles->EmitFromConfig(config);
}

void Bullet::EmitImpactVfx() {
    // 衝突と強制終了が重なっても着弾演出を重複発生させない。
    if (impactEmitted_ || (impactPreset_.empty() && impactSecondaryPreset_.empty() &&
                           impactDebrisPreset_.empty() && impactDebrisSecondaryPreset_.empty())) {
        return;
    }

    impactEmitted_ = true;
    engine::Vector3 direction = velocity_;

    if (engine::Math::Length(direction) <= 0.001f) {
        direction = {0.0f, 1.0f, 0.0f};
    } else {
        direction = engine::Math::Normalize(direction);
    }

    if (auto* particles = GPUParticleManager::GetInstance(); particles && particles->IsInitialized()) {
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

    if (auto* debris = DebrisEffectManager::GetInstance()) {
        for (const std::string* preset : {&impactDebrisPreset_, &impactDebrisSecondaryPreset_}) {
            if (preset->empty()) {
                continue;
            }

            DebrisEffectConfig config;
            const auto found = debris->GetPresets().find(*preset);
            if (found != debris->GetPresets().end()) {
                config = found->second;
            } else if (!debris->LoadConfig("Resources/json/debris/" + *preset + ".json", config)) {
                continue;
            } else {
                debris->RegisterPreset(*preset, config);
            }

            // Editorのプリセット値を基準に、弾の半径へ飛散範囲と欠片の大きさを追従させます。
            const float scale = impactDebrisScale_;
            config.scaleMin *= scale;
            config.scaleMax *= scale;
            config.spawnOffset = config.spawnOffset * scale;
            config.spawnBoxHalfExtents = config.spawnBoxHalfExtents * scale;
            config.speedMin *= std::sqrt(scale);
            config.speedMax *= std::sqrt(scale);
            engine::Vector3 scatterDirection = {-direction.x, 0.0f, -direction.z};
            if (engine::Math::Length(impactNormal_) > 0.001f) {
                engine::Vector3 wallNormal = {impactNormal_.x, 0.0f, impactNormal_.z};
                if (engine::Math::Length(wallNormal) > 0.001f) {
                    wallNormal = engine::Math::Normalize(wallNormal);
                    if (wallNormal.x * scatterDirection.x + wallNormal.z * scatterDirection.z < 0.0f)
                        wallNormal = wallNormal * -1.0f;
                    scatterDirection = wallNormal;
                    config.horizontalSpread *= 0.45f;
                }
            }
            config.baseDirection = scatterDirection;
            debris->SpawnFromConfig(config, GetTranslate());
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
    const engine::Vector3 center = GetTranslate();
    EmitImpactVfx();

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
        const engine::Vector3 ringPosition = ground.isHit ? ground.hitPoint + engine::Vector3{0, 0.06f, 0} : center;
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
    std::unordered_set<engine::Object3d*> damaged;

    for (const auto& hit : CollisionManager::GetInstance()->OverlapSphere(center, explosionRadius_, filter)) {
        if (!hit.object || !damaged.insert(hit.object).second)
            continue;
        engine::Vector3 delta = hit.object->GetWorldPosition() - center;
        PhysicsQueryFilter wallFilter;
        wallFilter.mask = CollisionCategories::Get("ground");
        wallFilter.ignoredObject = this;
        const float distance = engine::Math::Length(delta);

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

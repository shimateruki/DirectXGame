#pragma once
#include "game/actor/combat/CombatMath.h"
#include "CopyAbilitySession.h"
#define NOMINMAX
#include "game/actor/player/copy/PlayerCopyAbilityController.h"

#include "BaseEnemy.h"
#include "BaseScene.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "DebrisEffectManager.h"
#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include "EnemyBomb.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/system/CrownFinaleTuning.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "HitEffectDirector.h"
#include "MeshEffectManager.h"
#include "Player.h"
#include "game/actor/player/state/PlayerState.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <utility>

// 命中・演出の共通処理。能力の状態は保持せず、各セッションが管理します。
namespace CopyAbilityDetail {
constexpr float kPi = 3.1415926535f;

using CombatMath::NormalizePlanar;

inline float SmoothStep(float value) { return CombatMath::SmoothStep01(value); }

inline void SpawnEffect(const char* path, const Vector3& position, const Vector3& rotation = {},
                        const Vector3& scale = {1.0f, 1.0f, 1.0f}) {
    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        effects->SpawnEffectAt(path, position, rotation, scale);
    }
}

inline void SpawnScopedEffect(MeshEffectManager::EffectScopeId scopeId, const char* path, const Vector3& position,
                              const Vector3& rotation = {}, const Vector3& scale = {1.0f, 1.0f, 1.0f}) {
    if (MeshEffectManager* effects = MeshEffectManager::GetInstance()) {
        effects->SpawnEffectAtScoped(scopeId, path, position, rotation, scale);
    }
}

inline void EmitPreset(const char* preset, const Vector3& position) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized()) {
        particles->Emit(preset, position);
    }
}

inline void EmitDirectedPreset(const char* preset, const Vector3& position, const Vector3& direction,
                               float scale = 1.0f) {
    GPUParticleManager* particles = GPUParticleManager::GetInstance();
    if (particles && particles->IsInitialized()) {
        particles->EmitDirected(preset, position, direction, scale);
    }
}

inline void SpawnDebrisOnGround(const char* presetName, const Vector3& position, float lift = 0.04f) {
    const Vector3 groundPosition = HitEffectDirector::ResolveGroundEffectPosition(position);
    Vector3 effectPosition = groundPosition;
    effectPosition.y += lift;
    if (DebrisEffectManager* debris = DebrisEffectManager::GetInstance()) {
        debris->SpawnOnGround(presetName, effectPosition, groundPosition.y);
    }
}

inline Object3d* FindEnemyRoot(Object3d* object) {
    for (Object3d* current = object; current; current = current->GetParent()) {
        if (dynamic_cast<BaseEnemy*>(current)) {
            return current;
        }
    }
    return nullptr;
}

inline void DamageEnemy(Player& player, Object3d* target, float damage, DamageType type, const Vector3& knockback,
                        const StatusEffectApplication& status = {}) {
    if (!target) {
        return;
    }
    DamageEvent event;
    event.target = target;
    event.attacker = &player;
    event.damageAmount = damage;
    event.damageType = type;
    event.knockbackVelocity = knockback;
    event.statusEffect = status;
    EventManager::GetInstance()->Dispatch(event);
}

// 重複 Collider を持つ敵には一度だけダメージを送ります。
inline void DamageSphere(Player& player, const Vector3& center, float radius, float damage, DamageType type,
                         float horizontalKnockback, float verticalKnockback) {
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("enemy");
    filter.ignoredObject = &player;
    std::unordered_set<Object3d*> damaged;
    for (const PhysicsOverlapHit& hit : CollisionManager::GetInstance()->OverlapSphere(center, radius, filter)) {
        Object3d* target = FindEnemyRoot(hit.object);
        if (!target || !damaged.insert(target).second) {
            continue;
        }
        const Vector3 direction = NormalizePlanar(target->GetWorldPosition() - center);
        DamageEnemy(player, target, damage, type,
                    {direction.x * horizontalKnockback, verticalKnockback, direction.z * horizontalKnockback});
    }
}

inline const EnemyAttackDefinition& FindAttackOrFallback(const EnemyAttackProfile& profile, const char* id,
                                                         const EnemyAttackDefinition& fallback) {
    const EnemyAttackDefinition* attack = profile.FindAttack(id);
    return attack ? *attack : fallback;
}

inline StatusEffectApplication MakeBurnStatus(const EnemyAttackDefinition& attack) {
    StatusEffectApplication status;
    if (attack.statusEffectType == "burning") {
        status.type = StatusEffectType::Burning;
    }
    status.duration = attack.statusDuration;
    status.tickInterval = attack.statusTickInterval;
    status.tickDamage = attack.statusTickDamage;
    status.vfxPreset = attack.statusVfx;
    return status;
}

// 炎スライムのコピー能力が発射する弾の見た目と着弾演出を組み立てます。
inline BulletVisualConfig MakeFireVisual() {
    BulletVisualConfig visual;
    visual.materialType = 11;
    visual.color = {1.0f, 0.34f, 0.07f, 0.96f};
    visual.emissive = 2.8f;
    visual.visualScale = 1.28f;
    visual.effectType = 1.0f;
    visual.effectScale = 1.15f;
    visual.effectSoftness = 0.42f;
    visual.effectIntensity = 0.96f;
    visual.billboardScale = 0.68f;
    visual.trailPreset = "fireball_trail_flame";
    visual.trailSecondaryPreset = "fireball_trail_embers";
    visual.impactPreset = "fireball_impact_flame";
    visual.impactSecondaryPreset = "fireball_impact_sparks";
    visual.trailInterval = 0.025f;
    visual.trailSpeedScale = 1.08f;
    visual.trailReverseDirection = true;
    visual.impactDirected = false;
    return visual;
}

} // namespace CopyAbilityDetail

#include "HitEffectDirector.h"

#include "DebrisEffectManager.h"
#include "GPUParticleManager.h"
#include "GroundEffectLocator.h"
#include "MeshEffectManager.h"
#include "Object3d.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr const char* kSlimeElasticPreset = "hit_slime_elastic";
constexpr const char* kPullBindPreset = "hit_pull_bind";
constexpr const char* kPullCatchPreset = "hit_pull_catch";
constexpr const char* kThrowSlamDustPreset = "hit_throw_slam_dust";
constexpr const char* kThrowSlamDebrisPreset = "throw_slam_pebble_burst";
constexpr const char* kBombFireCorePreset = "hit_bomb_fire_core";
constexpr const char* kBombSparkPreset = "hit_bomb_fire_sparks";
constexpr const char* kBombGoldenSparkPreset = "hit_bomb_golden_sparks";
constexpr const char* kBombSmokePreset = "hit_bomb_black_smoke";
constexpr const char* kBombDebrisPreset = "bomb_hit_fragment_burst";
constexpr const char* kEnemyAbilityPreset = "hit_enemy_ability";
constexpr const char* kThunderShockPreset = "thunder_slime_idle_spark";
constexpr const char* kPlayerFireFlamePreset = "hit_player_fire_flame";
constexpr const char* kPlayerFireEmberPreset = "hit_player_fire_embers";
constexpr const char* kPlayerExplosionCorePreset = "hit_player_explosion_core";
constexpr const char* kPlayerExplosionSparkPreset = "hit_player_explosion_sparks";
constexpr const char* kPlayerExplosionSmokePreset = "hit_player_explosion_smoke";
constexpr const char* kDamagePuniBurstSequence = "damage_puni_burst_cue";
constexpr const char* kSlimeElasticSequence = "slime_elastic_hit_cue";
constexpr const char* kPullBindSequence = "pull_bind_cue";
constexpr const char* kPullCatchSequence = "pull_catch_cue";
constexpr const char* kThrowSlamSequence = "throw_slam_cue";
constexpr const char* kBombExplosionSequence = "bomb_explosion_cue";
constexpr const char* kEnemyAbilitySequence = "enemy_ability_hit_cue";
constexpr float kGroundEffectLift = 0.035f;

bool IsTinyPlanar(const Vector3& value);
Vector3 NormalizePlanar(Vector3 value);

void SpawnThunderShockHitBurst(Object3d* target) {
    auto* manager = GPUParticleManager::GetInstance();
    if (!target || !manager || !manager->IsInitialized()) {
        return;
    }

    const Vector3 scale = target->GetScale();
    const float maxXZ = (std::max)({ 1.0f, std::abs(scale.x), std::abs(scale.z) });
    const float yScale = (std::max)(std::abs(scale.y), 0.18f);
    const float horizontalDiameter = (std::max)(2.55f, maxXZ * 1.35f);
    const float verticalDiameter = (std::max)(0.46f, yScale * 1.28f);
    Vector3 center = target->GetWorldPosition();
    center.y += (std::max)(0.22f, yScale * 0.43f);

    const float horizontalRadius = horizontalDiameter * 0.52f;
    const float verticalRadius = verticalDiameter * 0.38f;
    constexpr int kSparkCount = 5;
    constexpr float kTwoPi = 6.283185307f;
    for (int i = 0; i < kSparkCount; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(kSparkCount) + 0.28f;
        Vector3 pos = center;
        pos.x += std::cos(angle) * horizontalRadius;
        pos.z += std::sin(angle) * horizontalRadius;
        pos.y += std::sin(angle * 1.37f) * verticalRadius;
        manager->Emit(kThunderShockPreset, pos);
    }
}

Vector3 ResolveBodyHitPosition(Object3d* target, Object3d* attacker, const Vector3& knockbackVelocity) {
    Vector3 awayDirection = knockbackVelocity;
    awayDirection.y = 0.0f;
    if (IsTinyPlanar(awayDirection) && target && attacker) {
        awayDirection = target->GetWorldPosition() - attacker->GetWorldPosition();
        awayDirection.y = 0.0f;
    }
    awayDirection = NormalizePlanar(awayDirection);

    const Vector3 scale = target ? target->GetScale() : Vector3{ 1.0f, 1.0f, 1.0f };
    const float bodyScale = (std::max)({ 1.0f, std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
    Vector3 position = target ? target->GetWorldPosition() : Vector3{};
    position += awayDirection * bodyScale * 0.34f;
    position.y += (std::max)(0.56f, std::abs(scale.y) * 0.44f);
    return position;
}

void SpawnFireDamageHit(Object3d* target, Object3d* attacker, const Vector3& knockbackVelocity) {
    auto* manager = GPUParticleManager::GetInstance();
    if (!target || !manager || !manager->IsInitialized()) {
        return;
    }

    const Vector3 position = ResolveBodyHitPosition(target, attacker, knockbackVelocity);
    Vector3 direction = knockbackVelocity;
    direction.y = (std::max)(direction.y, 0.55f);
    if (Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 1.0f, 0.0f };
    }
    manager->EmitDirected(kPlayerFireFlamePreset, position, direction, 1.0f);
    manager->EmitDirected(kPlayerFireEmberPreset, position, direction, 1.0f);
}

void SpawnExplosionDamageHit(Object3d* target, Object3d* attacker, const Vector3& knockbackVelocity) {
    auto* manager = GPUParticleManager::GetInstance();
    if (!target || !manager || !manager->IsInitialized()) {
        return;
    }

    const Vector3 position = ResolveBodyHitPosition(target, attacker, knockbackVelocity);
    Vector3 direction = knockbackVelocity;
    direction.y = (std::max)(direction.y, 0.42f);
    if (Math::Length(direction) <= 0.001f) {
        direction = { 0.0f, 1.0f, 0.0f };
    }
    manager->EmitDirected(kPlayerExplosionCorePreset, position, direction, 1.0f);
    manager->EmitDirected(kPlayerExplosionSparkPreset, position, direction, 1.0f);
    manager->EmitDirected(kPlayerExplosionSmokePreset, position, { 0.0f, 1.0f, 0.0f }, 1.0f);
}

bool IsTinyPlanar(const Vector3& value) {
    return std::fabs(value.x) < 0.001f && std::fabs(value.z) < 0.001f;
}

Vector3 NormalizePlanar(Vector3 value) {
    value.y = 0.0f;
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length <= 0.001f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { value.x / length, 0.0f, value.z / length };
}

void SpawnPlayerPuniDamageHit(Object3d* target, Object3d* attacker, const Vector3& knockbackVelocity) {
    if (!target) {
        return;
    }

    Vector3 awayDirection = knockbackVelocity;
    awayDirection.y = 0.0f;
    if (IsTinyPlanar(awayDirection) && attacker) {
        awayDirection = target->GetWorldPosition() - attacker->GetWorldPosition();
        awayDirection.y = 0.0f;
    }
    awayDirection = NormalizePlanar(awayDirection);

    const Vector3 incomingDirection = { -awayDirection.x, 0.0f, -awayDirection.z };
    const Vector3 scale = target->GetScale();
    const float bodyScale = (std::max)({ 1.0f, scale.x, scale.y, scale.z });
    Vector3 worldOffset = {
        incomingDirection.x * bodyScale * 0.52f,
        (std::max)(0.92f, bodyScale * 0.70f),
        incomingDirection.z * bodyScale * 0.52f
    };
    const Vector3 effectPosition = target->GetWorldPosition() + worldOffset;

    const float readableScale = bodyScale * 1.32f;
    const Vector3 effectScale = { readableScale, readableScale, readableScale };
    const Vector3 effectRotation = { 0.0f, std::atan2(incomingDirection.x, incomingDirection.z), 0.0f };

    VFXSequencer::PlayOneShot(kDamagePuniBurstSequence, effectPosition, effectScale, effectRotation);
}
}

void HitEffectDirector::SpawnSlimeElasticHit(const Vector3& position) {
    VFXSequencer::PlayOneShot(kSlimeElasticSequence, position);
}

void HitEffectDirector::SpawnPullBindHit(const Vector3& position) {
    VFXSequencer::PlayOneShot(kPullBindSequence, position);
}

void HitEffectDirector::SpawnPullCatchHit(const Vector3& position) {
    VFXSequencer::PlayOneShot(kPullCatchSequence, position);
}

void HitEffectDirector::SpawnThrowSlamShockwave(const Vector3& position, float impactSpeed) {
    const float power = (std::max)(0.75f, (std::min)(1.45f, impactSpeed / 22.0f));
    const Vector3 groundPos = ResolveGroundEffectPosition(position);
    Vector3 effectPos = groundPos;
    effectPos.y += kGroundEffectLift;

    VFXSequencer::PlayOneShot(kThrowSlamSequence, effectPos);
    SpawnDebris(kThrowSlamDebrisPreset, effectPos, groundPos.y);

    if (auto* meshEffect = MeshEffectManager::GetInstance()) {
        meshEffect->SpawnRingWaveEffect(effectPos);
        if (power > 1.1f) {
            meshEffect->SpawnRingWaveEffect(effectPos + Vector3{ 0.0f, 0.02f, 0.0f });
        }
    }
}

void HitEffectDirector::SpawnBombExplosionHit(const Vector3& position) {
    const Vector3 groundPos = ResolveGroundEffectPosition(position);
    const Vector3 effectBase = groundPos + Vector3{ 0.0f, kGroundEffectLift, 0.0f };

    VFXSequencer::PlayOneShot(kBombExplosionSequence, effectBase);
    SpawnDebris(kBombDebrisPreset, effectBase, groundPos.y);
}

void HitEffectDirector::SpawnEnemyAbilityHit(const Vector3& position) {
    VFXSequencer::PlayOneShot(kEnemyAbilitySequence, position);
}

void HitEffectDirector::SpawnDamageEventHit(
    Object3d* target,
    Object3d* attacker,
    const Vector3& knockbackVelocity,
    DamageType damageType) {
    if (!target || !attacker) {
        return;
    }

    const std::string attackerClass = attacker->GetClassName();
    const std::string targetClass = target->GetClassName();
    if (damageType == DamageType::Fire) {
        SpawnFireDamageHit(target, attacker, knockbackVelocity);
        return;
    }
    if (damageType == DamageType::Electric) {
        SpawnThunderShockHitBurst(target);
        return;
    }
    if (damageType == DamageType::Explosion) {
        SpawnExplosionDamageHit(target, attacker, knockbackVelocity);
        return;
    }

    if (targetClass == "Player" && attackerClass != "Player") {
        SpawnPlayerPuniDamageHit(target, attacker, knockbackVelocity);
        return;
    }

    if (attackerClass == "Player" && targetClass != "Player" && IsAlmostZero(knockbackVelocity)) {
        SpawnSlimeElasticHit(target->GetWorldPosition());
    }
}

Vector3 HitEffectDirector::ResolveGroundEffectPosition(const Vector3& position) {
    return GroundEffectLocator::ResolveGroundPosition(position);
}

void HitEffectDirector::EmitGpu(const char* presetName, const Vector3& position) {
    auto* manager = GPUParticleManager::GetInstance();
    if (!manager || !manager->IsInitialized()) {
        return;
    }
    manager->Emit(presetName, position);
}

void HitEffectDirector::SpawnDebris(const char* presetName, const Vector3& position, float groundY) {
    if (auto* manager = DebrisEffectManager::GetInstance()) {
        manager->SpawnOnGround(presetName, position, groundY);
    }
}

bool HitEffectDirector::IsAlmostZero(const Vector3& value) {
    return std::fabs(value.x) < 0.001f && std::fabs(value.y) < 0.001f && std::fabs(value.z) < 0.001f;
}

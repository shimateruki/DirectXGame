#pragma once

#include "engine/utility/math/Math.h"
#include "Event.h"

class Object3d;

// ダメージや攻撃ヒットに応じて、適切な GPU 粒子/破片エフェクトを呼び出す窓口
class HitEffectDirector {
public:
    static void SpawnSlimeElasticHit(const Vector3& position);
    static void SpawnPullBindHit(const Vector3& position);
    static void SpawnPullCatchHit(const Vector3& position);
    static void SpawnThrowSlamShockwave(const Vector3& position, float impactSpeed);
    static void SpawnBombExplosionHit(const Vector3& position);
    static void SpawnEnemyAbilityHit(const Vector3& position);

    static void SpawnDamageEventHit(
        Object3d* target,
        Object3d* attacker,
        const Vector3& knockbackVelocity,
        DamageType damageType = DamageType::Physical);
    static Vector3 ResolveGroundEffectPosition(const Vector3& position);

private:
    static void EmitGpu(const char* presetName, const Vector3& position);
    static void SpawnDebris(const char* presetName, const Vector3& position, float groundY);
    static bool IsAlmostZero(const Vector3& value);
};

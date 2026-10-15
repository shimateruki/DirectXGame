#pragma once

#include "engine/utility/math/Math.h"
#include "Event.h"

namespace engine { class Object3d; }

// ダメージや攻撃ヒットに応じて、適切な GPU 粒子/破片エフェクトを呼び出す窓口
class HitEffectDirector {
  public:
    static void SpawnSlimeElasticHit(const engine::Vector3& position);
    static void SpawnPullBindHit(const engine::Vector3& position);
    static void SpawnPullCatchHit(const engine::Vector3& position);
    // impactSpeed は衝突速度。地面位置を探して衝撃波と破片を配置する。
    static void SpawnThrowSlamShockwave(const engine::Vector3& position, float impactSpeed);
    static void SpawnBombExplosionHit(const engine::Vector3& position);
    static void SpawnEnemyAbilityHit(const engine::Vector3& position);

    // ダメージ成立後に呼ぶ。攻撃者・対象のどちらかがなければ何も出さない。
    // knockbackVelocity は打撃面の向きとエフェクト配置に使うワールド空間の速度。
    static void SpawnDamageEventHit(engine::Object3d* target, engine::Object3d* attacker, const engine::Vector3& knockbackVelocity,
                                    DamageType damageType = DamageType::Physical);
    static engine::Vector3 ResolveGroundEffectPosition(const engine::Vector3& position);

  private:
    static void EmitGpu(const char* presetName, const engine::Vector3& position);
    static void SpawnDebris(const char* presetName, const engine::Vector3& position, float groundY);
    static bool IsAlmostZero(const engine::Vector3& value);
};

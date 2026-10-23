#pragma once

#include "engine/utility/math/Math.h"
#include "Event.h"

namespace engine { class Object3d; }

// ダメージや攻撃ヒットに応じて、適切な GPU 粒子/破片エフェクトを呼び出す窓口
class HitEffectDirector {
  public:
    // 弾力のある命中演出はVFXシーケンス側の時間設定で再生する。
    static void SpawnSlimeElasticHit(const engine::Vector3& position);

    // 引き寄せの拘束開始を単発シーケンスへ委ねる。
    static void SpawnPullBindHit(const engine::Vector3& position);

    // 対象を捕まえた時点の演出を拘束開始とは別に再生する。
    static void SpawnPullCatchHit(const engine::Vector3& position);

    // impactSpeed は衝突速度。地面位置を探して衝撃波と破片を配置する。
    static void SpawnThrowSlamShockwave(const engine::Vector3& position, float impactSpeed);

    // 爆発と破片の起点を地面に合わせ、空中へ沈んだ演出を防ぐ。
    static void SpawnBombExplosionHit(const engine::Vector3& position);

    // 能力攻撃の共通ヒット演出は一つのシーケンス名から解決する。
    static void SpawnEnemyAbilityHit(const engine::Vector3& position);

    // ダメージ成立後に呼ぶ。攻撃者・対象のどちらかがなければ何も出さない。
    // knockbackVelocity は打撃面の向きとエフェクト配置に使うワールド空間の速度。
    static void SpawnDamageEventHit(engine::Object3d* target, engine::Object3d* attacker, const engine::Vector3& knockbackVelocity,
                                    DamageType damageType = DamageType::Physical);

    // 地面に広がる衝撃波・破片の高さを共通Locatorで求める。
    static engine::Vector3 ResolveGroundEffectPosition(const engine::Vector3& position);

  private:
    // 先読み前などManager未初期化の場面では放出を見送る。
    static void EmitGpu(const char* presetName, const engine::Vector3& position);

    // 地面の高さを明示して破片の着地位置を決める。
    static void SpawnDebris(const char* presetName, const engine::Vector3& position, float groundY);

    static bool IsAlmostZero(const engine::Vector3& value);
};

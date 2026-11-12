#pragma once

// EnemyMagmaSlimeの分割実装で共有する定数と攻撃用の計算。進行状態はEnemyMagmaSlimeが保持する。

#include "game/actor/enemy/boss/magma/EnemyMagmaSlime.h"
#include "game/actor/enemy/boss/magma/MagmaAttackVfx.h"
#include <numbers>
#include "game/actor/combat/CombatMath.h"

#include <string>

// マグマボスの複数実装ファイルで共有する攻撃ID、タイミング、演出資産です。
namespace EnemyMagmaSlimeImplementation {
constexpr const char* kMagmaMortarAttackId = MagmaAttackVfx::kMortar.attackId;
constexpr const char* kLavaRushAttackId = "lava_rush";
constexpr const char* kVolcanicSlamAttackId = "volcanic_slam";
constexpr const char* kEruptionFieldAttackId = "eruption_field";
constexpr const char* kLavaSpiralAttackId = "lava_spiral";
constexpr const char* kFireColumnAttackId = MagmaAttackVfx::kFireColumn.attackId;
constexpr const char* kMagmaFloodAttackId = MagmaAttackVfx::kFlood.attackId;

constexpr const char* kFireChantEffect = "Resources/json/effect/effect_boss_chant_fire.json";
constexpr const char* kFireColumnLinkEffect = "Resources/json/effect/effect_magma_fire_column_link.json";
constexpr const char* kMagmaChantEffect = "Resources/json/effect/effect_boss_chant_magma.json";

constexpr const char* kFireChantMotesPreset = "boss_chant_fire_motes";

constexpr const char* kCoreEmberPreset = "magma_slime_core_embers";
constexpr const char* kChargePreset = "magma_slime_charge";

constexpr const char* kImpactPreset = "magma_slime_impact";

constexpr const char* kRushWakePreset = "magma_slime_rush_wake";
constexpr const char* kRushSplashPreset = "magma_slime_rush_splash";

constexpr const char* kSlamBurstPreset = "magma_slime_slam_burst";

constexpr const char* kGeyserBurstPreset = "magma_slime_geyser_burst";
constexpr const char* kGeyserStreamPreset = "magma_slime_geyser_stream";

constexpr const char* kSpiralSurgePreset = "magma_slime_spiral_surge";
constexpr const char* kSpiralTrailPreset = "magma_slime_spiral_trail";


constexpr int kMagmaMaterialType = 9;
constexpr int kFireMaterialType = 11;
constexpr int kShockwaveMaterialType = 14;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kMagmaSlimeModelYawOffset = kPi;

// モデルの拡縮にかかわらず維持する、ボス本体の当たり判定半径（ワールド単位）。
constexpr float kGroundCollisionWorldRadius = 3.65f;

// 連続する溶岩弾の発射間隔と、飛行中の軌跡を出す間隔（秒）。
constexpr float kMortarInterval = 0.085f;
constexpr float kMortarTrailInterval = 0.055f;

constexpr float kRushWakeInterval = 0.045f;
constexpr float kRushPoolInterval = 0.115f;

constexpr float kEruptionInterval = 0.135f;

constexpr float kPoolDamageInterval = 0.72f;
// 地面の演出に対し、標的が上下に離れすぎているかを判定する許容距離。
constexpr float kPoolVerticalTolerance = 2.8f;

constexpr int kEncounterControlledActionMode = 1;
constexpr float kDefaultEncounterAppearanceDuration = 1.16f;
// 登場開始時に配置位置から上へずらす距離。演出の進行に合わせて戻す。
constexpr float kEncounterAppearanceRise = 2.35f;

using CombatMath::NormalizePlanar;

using CombatMath::PlanarDistance;

using CombatMath::SmoothStep01;

using CombatMath::EaseOutCubic;

using CombatMath::LerpVector3;

inline StatusEffectApplication MakeBurningStatus(float duration, float tickInterval, float tickDamage,
                                          const std::string& vfxPreset) {
    StatusEffectApplication status;
    status.type = StatusEffectType::Burning;
    status.duration = duration;
    status.tickInterval = tickInterval;
    status.tickDamage = tickDamage;
    status.vfxPreset = vfxPreset;
    return status;
}
} // namespace EnemyMagmaSlimeImplementation

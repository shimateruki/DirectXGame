#pragma once
// 分割実装専用の定数・補助関数。外部からは公開ヘッダーを使用します。
#define NOMINMAX
#include "game/actor/combat/CombatMath.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"

#include "BulletManager.h"
#include "Character.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "DebrisEffectManager.h"
#include "EffectObject3d.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "game/actor/enemy/animation/SlimeBounceAnimator.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace EnemyPrismSlimeImplementation {
// 攻撃プロフィールの ID。JSON 側と一致させます。
inline constexpr const char* kCrystalSpikesAttackId = "crystal_spikes";
inline constexpr const char* kCrystalLanceVolleyAttackId = "crystal_lance_volley";
inline constexpr const char* kFireFanAttackId = "fire_fan";
inline constexpr const char* kThunderChainAttackId = "thunder_chain";
inline constexpr const char* kWindWaveAttackId = "wind_wave";
inline constexpr const char* kSlimeSummonAttackId = "slime_summon";

// パーティクル、メッシュ効果、魔法陣の資産名。
inline constexpr const char* kPrismChargePreset = "prism_slime_charge";
inline constexpr const char* kPrismPhasePulsePreset = "prism_slime_pulse";
inline constexpr const char* kPrismSpikeWarningPreset = "prism_spike_warning";
inline constexpr const char* kPrismSpikeBurstPreset = "prism_spike_burst";
inline constexpr const char* kPrismSpikeShatterDebrisPreset = "prism_crystal_shatter";
inline constexpr const char* kPrismLanceTrailPreset = "prism_lance_trail";
inline constexpr const char* kPrismSpellMotesPreset = "prism_spell_motes";
inline constexpr const char* kFireChantMotesPreset = "boss_chant_fire_motes";
inline constexpr const char* kThunderChantMotesPreset = "boss_chant_thunder_motes";
inline constexpr const char* kWindChantMotesPreset = "boss_chant_wind_motes";
inline constexpr const char* kPrismLanceReleasePreset = "prism_lance_release";
inline constexpr const char* kPrismSpellTelegraphTexture = "Resources/sprite/effect/prism/prism_spell_circle.dds";
inline constexpr const char* kPrismCrystalLanceCircleEffect =
    "Resources/json/effect/effect_prism_crystal_lance_circle.json";
inline constexpr const char* kPrismChantSigilEffect = "Resources/json/effect/effect_prism_chant_sigil.json";
inline constexpr const char* kFireChantSigilEffect = "Resources/json/effect/effect_boss_chant_fire.json";
inline constexpr const char* kThunderChantSigilEffect = "Resources/json/effect/effect_boss_chant_thunder.json";
inline constexpr const char* kWindChantSigilEffect = "Resources/json/effect/effect_boss_chant_wind.json";
inline constexpr const char* kPrismSpikeGroundEffect = "Resources/json/effect/effect_prism_spike_ground_flash.json";
inline constexpr const char* kPrismSpikeSectorEffect = "Resources/json/effect/effect_prism_spike_sector.json";
inline constexpr const char* kFireCastPreset = "fire_slime_cast";
inline constexpr const char* kThunderChargePreset = "thunder_slime_charge";
inline constexpr const char* kThunderImpactPreset = "player_thunder_strike_impact";
inline constexpr const char* kWindChargePreset = "wind_slime_charge";
inline constexpr const char* kWindStreamPreset = "wind_slime_breath_stream";
inline constexpr const char* kWindDustPreset = "wind_slime_breath_dust";
inline constexpr const char* kWindImpactPreset = "wind_slime_gust_impact";

inline constexpr const char* kThunderWarningEffect = "Resources/json/effect/effect_player_thunder_warning.json";
inline constexpr const char* kThunderBoltEffect = "Resources/json/effect/effect_player_thunder_bolt.json";
inline constexpr const char* kThunderCoreEffect = "Resources/json/effect/effect_player_thunder_core.json";
inline constexpr const char* kThunderImpactEffect = "Resources/json/effect/effect_player_thunder_impact_ring.json";
inline constexpr const char* kThunderScorchEffect = "Resources/json/effect/effect_thunder_scorch_mark.json";
inline constexpr const char* kWindRingEffect = "Resources/json/effect/effect_wind_gust_ring.json";
inline constexpr const char* kPrismWindTornadoOuterEffect = "Resources/json/effect/effect_player_wind_tornado.json";
inline constexpr const char* kPrismWindTornadoInnerEffect =
    "Resources/json/effect/effect_player_wind_tornado_inner.json";

// 素材、衝突、技の発生間隔、遭遇演出に使う調整値。
inline constexpr int kFireMaterialType = 11;
inline constexpr int kPrismCrystalMaterialType = 27;
inline constexpr float kPi = 3.1415926535f;
inline constexpr float kHalfPi = kPi * 0.5f;
inline constexpr float kGroundCollisionWorldRadius = 3.25f;
inline const uint32_t kPrismActiveCollisionAttribute = CollisionCategories::Get("enemy");
inline const uint32_t kPrismActiveCollisionMask = CollisionCategories::Get("player") | CollisionCategories::Get("ground") | CollisionCategories::Get("player_attack") | CollisionCategories::Get("player_bullet");
inline const uint32_t kPrismAppearanceCollisionMask = CollisionCategories::Get("ground");
inline constexpr float kPrismSpikeInterval = 0.011f;
inline constexpr float kPrismSpikeRiseDuration = 0.24f;
inline constexpr float kPrismSpikeHoldDuration = 1.64f;
inline constexpr float kPrismSpikeFractureDuration = 0.10f;
inline constexpr float kPrismSpikeLifetime =
    kPrismSpikeRiseDuration + kPrismSpikeHoldDuration + kPrismSpikeFractureDuration;
inline constexpr float kCrystalLanceInterval = 0.12f;
inline constexpr float kCrystalLanceTrailInterval = 0.045f;
inline constexpr int kCrystalLanceCount = 5;
inline constexpr float kFireFanInterval = 0.095f;
inline constexpr int kFireFanProjectileCount = 5;
inline constexpr float kThunderStrikeInterval = 0.115f;
inline constexpr float kWindPushInterval = 0.12f;
inline constexpr float kSummonInterval = 0.17f;
inline constexpr float kSummonPortalFadeDuration = 0.34f;
inline constexpr int kSummonPortalCount = 3;
inline constexpr int kMaxSummonWaves = 2;
inline constexpr int kEncounterControlledActionMode = 1;
inline constexpr float kDefaultEncounterAppearanceDuration = 1.08f;
inline constexpr float kEncounterAppearanceRise = 1.85f;
inline constexpr const char* kEncounterAppearanceSequence = "prism_midboss_appear_cue";
inline constexpr float kRepositionMinimumDistance = 4.5f;
inline constexpr float kRepositionMaximumDistance = 8.0f;
inline constexpr float kRepositionMoveSpeed = 5.8f;
inline constexpr float kRepositionJumpSpeed = 6.2f;
inline constexpr float kRepositionArenaHalfExtentX = 21.0f;
inline constexpr float kRepositionArenaHalfExtentZ = 18.0f;

// 召喚位置の選択を再現可能にする疑似乱数です。state を呼び出し元で保持します。
inline float NextSummonRandom01(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

using CombatMath::NormalizePlanar;

using CombatMath::SmoothStep01;

// 水平方向の攻撃基準ベクトルを指定角度だけ回転します。
inline Vector3 RotatePlanar(const Vector3& direction, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return NormalizePlanar({
        direction.x * cosine + direction.z * sine,
        0.0f,
        direction.z * cosine - direction.x * sine,
    });
}

// 上向きのモデル軸を進行方向へ向ける Euler 角を返します。
inline Vector3 MakeUpAxisAimRotation(const Vector3& direction) {
    const Vector3 normalized = Math::Normalize(direction);
    const float horizontalLength = std::sqrt(normalized.x * normalized.x + normalized.z * normalized.z);
    return {
        std::atan2(horizontalLength, normalized.y),
        std::atan2(normalized.x, normalized.z),
        0.0f,
    };
}

// 線分から点までの最短距離の二乗。射線との衝突判定に使用します。
inline float DistanceSquaredToSegment(const Vector3& point, const Vector3& start, const Vector3& end) {
    const Vector3 segment = end - start;
    const float lengthSquared = Math::Dot(segment, segment);
    if (lengthSquared <= 0.000001f) {
        const Vector3 difference = point - start;
        return Math::Dot(difference, difference);
    }
    const float rate = std::clamp(Math::Dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
    const Vector3 nearest = start + segment * rate;
    const Vector3 difference = point - nearest;
    return Math::Dot(difference, difference);
}

// プリズムが放つ火球の素材、軌跡、命中時の表示設定をまとめます。
inline BulletVisualConfig MakePrismFireVisual() {
    BulletVisualConfig visual;
    visual.materialType = kFireMaterialType;
    visual.blendMode = BlendMode::kNormal;
    visual.color = {1.0f, 0.30f, 0.055f, 0.98f};
    visual.emissive = 3.0f;
    visual.visualScale = 1.22f;
    visual.effectType = 1.0f;
    visual.effectScale = 1.18f;
    visual.effectSoftness = 0.38f;
    visual.effectIntensity = 1.08f;
    visual.billboardScale = 0.74f;
    visual.trailPreset = "fireball_trail_flame";
    visual.trailSecondaryPreset = "fireball_trail_embers";
    visual.impactPreset = "fireball_impact_flame";
    visual.impactSecondaryPreset = "fireball_impact_sparks";
    visual.trailInterval = 0.025f;
    visual.trailSpeedScale = 1.16f;
    visual.trailReverseDirection = true;
    visual.impactDirected = false;
    return visual;
}

// 攻撃プロフィールから燃焼状態異常へ必要な値を移します。
inline StatusEffectApplication MakeBurningStatus(const EnemyAttackDefinition& attack) {
    StatusEffectApplication status;
    status.type = StatusEffectType::Burning;
    status.duration = attack.statusDuration;
    status.tickInterval = attack.statusTickInterval;
    status.tickDamage = attack.statusTickDamage;
    status.vfxPreset = attack.statusVfx;
    return status;
}
} // namespace EnemyPrismSlimeImplementation

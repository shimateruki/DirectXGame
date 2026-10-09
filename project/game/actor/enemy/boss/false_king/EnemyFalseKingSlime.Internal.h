#pragma once
// 分割実装専用の定数・補助関数。外部からは公開ヘッダーを使用します。
#define NOMINMAX
#include "game/actor/combat/CombatMath.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"

#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "Camera.h"
#include "CameraManager.h"
#include "EventManager.h"
#include "GPUParticleManager.h"
#include "MeshRenderer.h"
#include "EffectObject3d.h"
#include "SceneManager.h"
#include "game/actor/enemy/animation/SlimeBounceAnimator.h"
#include "VFXSequencer.h"
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/system/CrownFinaleTuning.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cassert>
#include <cmath>

namespace EnemyFalseKingSlimeImplementation {
// 予兆と実弾が同じ本数・幅を参照します。幅は中心線からの半幅です。
inline int RoyalBeamCount(bool dominion, int phase) { return dominion ? 6 : (phase >= 3 ? 4 : 3); }
inline float RoyalBeamHalfWidth(bool dominion, float radius) { return radius * (dominion ? 0.90f : 1.0f); }

// 攻撃プロフィールの ID。JSON 側と一致させます。
inline constexpr const char* kCrownLanceRainAttackId = "crown_lance_rain";
inline constexpr const char* kRoyalShockwaveAttackId = "royal_shockwave";
inline constexpr const char* kKingRushAttackId = "king_rush";
inline constexpr const char* kRoyalCrossAttackId = "royal_cross";
inline constexpr const char* kCrownDominionAttackId = "crown_dominion";
inline constexpr const char* kUmbraFlameAttackId = "umbra_flame";
inline constexpr const char* kAstralReaverAttackId = "astral_reaver";
inline constexpr const char* kNightfallLancesAttackId = "nightfall_lances";
// 攻撃プロフィールには保存しない、第2形態の封印失敗演出用プレビュー ID。
inline constexpr const char* kSealBarragePreviewId = "seal_barrage_preview";

// 魔法陣と攻撃時の VFX 資産名。
inline constexpr const char* kUmbraFlameSigilEffect = "Resources/json/effect/effect_false_king_umbra_flame.json";
inline constexpr const char* kUmbraFlameNetworkEffect =
    "Resources/json/effect/effect_false_king_umbra_flame_network.json";
inline constexpr const char* kUmbraFlameChargePreset = "false_king_umbra_flame_charge";
inline constexpr const char* kUmbraFlameFlamePreset = "false_king_umbra_flame_flame";
inline constexpr const char* kAstralReaverOrbEffect = "Resources/json/effect/effect_false_king_astral_reaver_orb.json";
inline constexpr const char* kAstralReaverCutterHaloEffect =
    "Resources/json/effect/effect_false_king_astral_reaver_cutter_halo.json";
inline constexpr const char* kAstralReaverCutterBladeEffect =
    "Resources/json/effect/effect_false_king_astral_reaver_cutter_blade.json";
inline constexpr const char* kAstralReaverCutterArcEffect =
    "Resources/json/effect/effect_false_king_astral_reaver_cutter_arc.json";
inline constexpr const char* kAstralReaverRingEffect =
    "Resources/json/effect/effect_false_king_astral_reaver_ring.json";
inline constexpr const char* kAstralReaverTrailPreset = "false_king_astral_reaver_trail";
inline constexpr const char* kAstralReaverImpactPreset = "false_king_astral_reaver_impact";
inline constexpr const char* kNightfallLancesRingEffect =
    "Resources/json/effect/effect_false_king_nightfall_lances_ring.json";
inline constexpr const char* kNightfallLancesSkySigilEffect =
    "Resources/json/effect/effect_false_king_nightfall_lances_sky_sigil.json";
inline constexpr const char* kCrownTransferPulseEffect =
    "Resources/json/effect/effect_false_king_crown_transfer_pulse.json";
inline constexpr const char* kNightfallLancesCoreEffect =
    "Resources/json/effect/effect_false_king_nightfall_lances_core.json";
inline constexpr const char* kCrownBarrageSpearModel = "Effects/false_king_shadow_spear";
inline constexpr const char* kNightfallLancesGlowEffect =
    "Resources/json/effect/effect_false_king_nightfall_lances_glow.json";
inline constexpr const char* kNightfallLancesShaftEffect =
    "Resources/json/effect/effect_false_king_nightfall_lances_shaft.json";
inline constexpr const char* kNightfallGroundArcEffect =
    "Resources/json/effect/effect_false_king_nightfall_ground_arc.json";
inline constexpr const char* kNightfallImpactBeamEffect =
    "Resources/json/effect/effect_false_king_nightfall_impact_beam.json";
inline constexpr const char* kNightfallImpactLightningEffect =
    "Resources/json/effect/effect_false_king_nightfall_impact_lightning.json";
inline constexpr const char* kNightfallLancesChargePreset = "false_king_nightfall_lances_charge";
inline constexpr const char* kNightfallLancesTrailPreset = "false_king_nightfall_lances_trail";
inline constexpr const char* kNightfallLancesImpactPreset = "false_king_nightfall_lances_impact";
inline constexpr const char* kNightfallLancesRimPreset = "false_king_nightfall_lances_rim";
inline constexpr const char* kNightfallLancesDissolvePreset = "false_king_nightfall_lances_dissolve";

inline constexpr const char* kChargePreset = "false_king_charge";
inline constexpr const char* kLanceTrailPreset = "false_king_lance_trail";
inline constexpr const char* kLanceImpactPreset = "false_king_lance_impact";
inline constexpr const char* kRushWakePreset = "false_king_rush_wake";
inline constexpr const char* kShockwavePreset = "false_king_shockwave";
inline constexpr const char* kShockwaveChargeRingPreset = "false_king_shockwave_charge_ring";
inline constexpr const char* kShockwaveDustPreset = "false_king_shockwave_dust";
inline constexpr const char* kShockwaveCrestPreset = "false_king_shockwave_crest";
inline constexpr const char* kShockwaveShardPreset = "false_king_shockwave_shard";
inline constexpr const char* kDominionPreset = "false_king_dominion";
inline constexpr const char* kAppearanceSequence = "false_king_appear_cue";
inline constexpr const char* kPhaseShiftSequence = "false_king_phase_shift_cue";
inline constexpr const char* kDominionSequence = "false_king_dominion_cue";

// ボス専用の表示モデル。
inline constexpr const char* kCrownLanceModel = "Effects/false_king_crown_lance";
inline constexpr const char* kRoyalShockwaveWallModel = "Effects/false_king_shockwave_wall";
inline constexpr const char* kRoyalBeamModel = "Effects/false_king_royal_beam";
inline constexpr const char* kRushWingsModel = "Effects/false_king_rush_wings";
inline constexpr const char* kDominionSigilModel = "Effects/false_king_dominion_sigil";

// 素材、攻撃間隔、登場演出に共通する調整値。
inline constexpr int kEncounterControlledActionMode = 1;
inline constexpr int kLaserMaterialType = 12;
inline constexpr int kShockwaveMaterialType = 14;
inline constexpr int kCrownMaterialType = 19;
inline constexpr int kGatePortalMaterialType = 22;
inline constexpr int kFireMaterialType = 11;
inline constexpr float kDefaultAppearanceDuration = 1.45f;
inline constexpr float kAppearanceRise = 3.6f;
inline constexpr float kPi = 3.1415926535f;
inline constexpr float kTwoPi = kPi * 2.0f;
inline constexpr float kModelYawOffset = kPi;
inline constexpr float kLanceInterval = 0.13f;
inline constexpr float kDominionLanceInterval = 0.58f;

using CombatMath::SmoothStep01;

using CombatMath::EaseOutCubic;

using CombatMath::NormalizePlanar;

// ゼロベクトルなら fallback を返し、方向ベクトルの正規化を安定させます。
inline Vector3 SafeNormalize(const Vector3& value, const Vector3& fallback) {
    if (Math::Length(value) <= 0.001f) {
        return fallback;
    }
    return Math::Normalize(value);
}

// モデルの指定軸を攻撃方向へ合わせる回転を生成します。
inline Quaternion MakeAxisToDirectionQuaternion(const Vector3& axis, const Vector3& direction) {
    const Vector3 from = SafeNormalize(axis, {0.0f, 1.0f, 0.0f});
    const Vector3 to = SafeNormalize(direction, from);
    const float dot = std::clamp(Math::Dot(from, to), -1.0f, 1.0f);
    if (dot > 0.9999f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (dot < -0.9999f) {
        Vector3 perpendicular = Math::Cross(from, {1.0f, 0.0f, 0.0f});
        if (Math::Length(perpendicular) <= 0.001f) {
            perpendicular = Math::Cross(from, {0.0f, 0.0f, 1.0f});
        }
        perpendicular = Math::Normalize(perpendicular);
        return {perpendicular.x, perpendicular.y, perpendicular.z, 0.0f};
    }

    const Vector3 rotationAxis = Math::Cross(from, to);
    const float scale = std::sqrt((1.0f + dot) * 2.0f);
    const float inverseScale = 1.0f / scale;
    return {
        rotationAxis.x * inverseScale,
        rotationAxis.y * inverseScale,
        rotationAxis.z * inverseScale,
        scale * 0.5f,
    };
}

using CombatMath::PlanarDistance;

using CombatMath::LerpVector3;
} // namespace EnemyFalseKingSlimeImplementation

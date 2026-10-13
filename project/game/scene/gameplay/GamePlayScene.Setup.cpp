#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "ScenePreloader.h"
#include "SceneController.h"
#include "SceneManager.h"

#include "AudioPlayer.h"
#include "BulletManager.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebrisEffectManager.h"
#include "DirectXCommon.h"
#include "EventManager.h"
#include "Fade.h"
#include "GameDataManager.h"
#include "game/actor/gimmick/trigger/GimmickArenaEncounter.h"
#include "GameRule.h"
#include "GPUParticleManager.h"
#include "LevelLoader.h"
#include "LightEditor.h"
#include "LightManager.h"
#include "Log.h"
#include "LockOnSystem.h"
#include "MeshEffectManager.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ObjectManager.h"
#include "ParticleManager.h"
#include "ParticleSystem.h"
#include "Skybox.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "StageManager.h"
#include "TextureManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

// 操作ガイドは起動直後の初回表示でも画像のデコード待ちを出さないよう、
// 再生可能なコピー能力の肖像と操作ラベルをロード画面の対象に含めます。
constexpr const char* kControlsGuidePortraitsToPreload[] = {
    "Resources/sprite/ui/control_guide/portraits/pink_slime.png",
    "Resources/sprite/ui/control_guide/portraits/bomb_slime.png",
    "Resources/sprite/ui/control_guide/portraits/wind_slime.png",
    "Resources/sprite/ui/control_guide/portraits/fire_slime.png",
    "Resources/sprite/ui/control_guide/portraits/thunder_slime.png"};

constexpr const char* kControlsGuideAbilityLabelsToPreload[] = {
    // 共通操作と通常形態。
    "Resources/sprite/ui/control_guide/labels/absorb.png",
    "Resources/sprite/ui/control_guide/labels/throw.png",
    "Resources/sprite/ui/control_guide/labels/slime_attack.png",
    "Resources/sprite/ui/control_guide/labels/hook_aim.png",
    "Resources/sprite/ui/control_guide/labels/slime_dive.png",

    // 変身後の技は能力ごとに表示されるため、現在の能力だけに絞らず先読みします。
    "Resources/sprite/ui/control_guide/labels/puni_straight.png",
    "Resources/sprite/ui/control_guide/labels/puni_guard.png",

    "Resources/sprite/ui/control_guide/labels/bomb_throw.png",
    "Resources/sprite/ui/control_guide/labels/bomb_place.png",
    "Resources/sprite/ui/control_guide/labels/blast_jump.png",

    "Resources/sprite/ui/control_guide/labels/fireball.png",
    "Resources/sprite/ui/control_guide/labels/flame_breath.png",
    "Resources/sprite/ui/control_guide/labels/blaze_step.png",

    "Resources/sprite/ui/control_guide/labels/thunder_chain.png",
    "Resources/sprite/ui/control_guide/labels/charged_discharge.png",
    "Resources/sprite/ui/control_guide/labels/thunder_step.png",

    "Resources/sprite/ui/control_guide/labels/updraft.png",
    "Resources/sprite/ui/control_guide/labels/wind_breath.png",
    "Resources/sprite/ui/control_guide/labels/wind_dash.png"};

// ここはエフェクト定義の所有リストです。Stage JSONに現れない攻撃演出も、
// 初回発動時のファイル読込を避けるためGameplay開始前に登録します。
constexpr const char* kGameplayMeshEffectsToPreload[] = {
    // 爆発と通常／属性ダメージ。
    "Resources/json/effect/effect_bakuhatu.json",
    "Resources/json/effect/effect_bomb_core_flash.json",
    "Resources/json/effect/effect_bomb_shockwave_ring.json",
    "Resources/json/effect/effect_damage_puni_flash.json",
    "Resources/json/effect/effect_damage_puni_ring.json",
    "Resources/json/effect/effect_damage_galaxy_core_flash.json",
    "Resources/json/effect/effect_damage_galaxy_star_slash.json",
    "Resources/json/effect/effect_damage_galaxy_impact_ring.json",
    "Resources/json/effect/effect_hitfx_kickpunch_center_flash.json",
    "Resources/json/effect/effect_hitfx_kickpunch_star_burst.json",
    "Resources/json/effect/effect_hitfx_kickpunch_ground_glow.json",
    "Resources/json/effect/effect_hitfx_kickpunch_shock_arc.json",
    "Resources/json/effect/effect_hitfx_kickpunch_thin_streak.json",

    // 雷の常時表現、予兆、回避、着弾。
    "Resources/json/effect/effect_thunder_slime_constant_aura.json",
    "Resources/json/effect/effect_player_thunder_warning.json",
    "Resources/json/effect/effect_player_thunder_bolt.json",
    "Resources/json/effect/effect_player_thunder_core.json",
    "Resources/json/effect/effect_player_thunder_impact_ring.json",
    "Resources/json/effect/effect_player_thunder_evade_trail.json",
    "Resources/json/effect/effect_player_thunder_evade_cross.json",
    "Resources/json/effect/effect_player_thunder_evade_seal.json",
    "Resources/json/effect/effect_player_thunder_evade_burst.json",
    "Resources/json/effect/effect_thunder_charge_ground.json",
    "Resources/json/effect/effect_thunder_scorch_mark.json",

    // 中ボスの召喚・結界と撃破時の共通演出。
    "Resources/json/effect/effect_prism_spike_ground_flash.json",
    "Resources/json/effect/effect_prism_arena_seal_ring.json",
    "Resources/json/effect/effect_prism_midboss_summon_pillar.json",
    "Resources/json/effect/effect_prism_arena_release_wave.json",
    "Resources/json/effect/effect_enemy_defeat_core_flash.json",
    "Resources/json/effect/effect_enemy_defeat_pop_ring.json",

    // 運搬した敵とピンク形態の攻撃。
    "Resources/json/effect/effect_carry_bomber_throw_burst.json",
    "Resources/json/effect/effect_carry_eye_charge_ring.json",
    "Resources/json/effect/effect_carry_eye_beam_muzzle.json",
    "Resources/json/effect/effect_carry_bat_glide_ring.json",
    "Resources/json/effect/effect_pink_slime_charge_pulse_ring.json",
    "Resources/json/effect/effect_pink_slime_charge_core_flash.json",
    "Resources/json/effect/effect_pink_slime_charge_vortex_streak.json",
    "Resources/json/effect/effect_pink_slime_launch_kick_ring.json",
    "Resources/json/effect/effect_pink_slime_apex_focus_flash.json",
    "Resources/json/effect/effect_pink_slime_dive_streak.json",
    "Resources/json/effect/effect_pink_slime_landing_burst_ring.json",
    "Resources/json/effect/effect_pink_slime_landing_core_flash.json",
    "Resources/json/effect/effect_pink_slime_landing_shock_arc.json",

    // ステージ進行で使用するゲート、王冠、ゴール、チェックポイント。
    "Resources/json/effect/effect_warp_gate_floor.json",
    "Resources/json/effect/effect_warp_gate_pillar.json",
    "Resources/json/effect/effect_crown_idle_shell.json",
    "Resources/json/effect/effect_crown_get_flash_ring.json",
    "Resources/json/effect/effect_goal_clear_gold_ring.json",
    "Resources/json/effect/effect_goal_clear_gold_pillar.json",
    "Resources/json/effect/effect_goal_clear_silver_ring.json",
    "Resources/json/effect/effect_goal_clear_silver_pillar.json",
    "Resources/json/effect/effect_player_thunder_discharge_charge.json",
    "Resources/json/effect/effect_player_thunder_discharge_burst.json",
    "Resources/json/effect/effect_player_fire_blaze_trail.json",
    "Resources/json/effect/effect_player_fire_blaze_core.json",
    "Resources/json/effect/effect_player_fire_blaze_burst.json",
    "Resources/json/effect/effect_player_fire_blaze_body.json",
    "Resources/json/effect/effect_player_fire_blaze_ground_wake.json",
    "Resources/json/effect/effect_player_pink_straight_arc.json",
    "Resources/json/effect/effect_player_pink_straight_impact.json",
    "Resources/json/effect/effect_player_pink_bounce_launch.json",
    "Resources/json/effect/effect_player_pink_bounce_land.json",
    "Resources/json/effect/effect_player_base_bash_arc.json",
    "Resources/json/effect/effect_player_base_bash_impact.json",
    "Resources/json/effect/effect_player_base_press_impact.json",
    "Resources/json/effect/effect_checkpoint_activate_ring.json",
    "Resources/json/effect/effect_checkpoint_idle_ring.json",
    "Resources/json/effect/effect_checkpoint_activate_pillar.json",
    "Resources/json/effect/effect_copy_memory_transfer_ring.json",
    "Resources/json/effect/effect_copy_memory_transfer_pillar.json",

    // プレイヤーの防御・設置・変身・風移動。
    "Resources/json/effect/effect_player_pink_guard_start.json",
    "Resources/json/effect/effect_player_pink_guard_shell.json",
    "Resources/json/effect/effect_player_pink_guard_release.json",
    "Resources/json/effect/effect_player_bomb_place.json",
    "Resources/json/effect/effect_player_bomb_blast_jump.json",
    "Resources/json/effect/effect_player_bomb_blast_jump_trail.json",
    "Resources/json/effect/effect_player_bomb_blast_jump_land.json",
    "Resources/json/effect/effect_player_bomb_morph_pulse.json",
    "Resources/json/effect/effect_player_fire_morph_pulse.json",
    "Resources/json/effect/effect_player_wind_morph_pulse.json",
    "Resources/json/effect/effect_wind_gust_ring.json",
    "Resources/json/effect/effect_player_wind_updraft_spiral.json",
    "Resources/json/effect/effect_player_wind_updraft_spiral_counter.json",
    "Resources/json/effect/effect_player_wind_soar_launch.json",
    "Resources/json/effect/effect_player_wind_slow_fall.json",
    "Resources/json/effect/effect_player_wind_soar_land.json",

    // リングバーナーは波本体と熱の余韻を別定義で描きます。
    "Resources/json/effect/effect_ring_burner_charge.json",
    "Resources/json/effect/effect_ring_burner_wave.json",
    "Resources/json/effect/effect_ring_burner_wave_heat.json"};

// GPU粒子はMeshEffectと読込経路が異なるため、同名の攻撃に属しても別一覧で管理します。
constexpr const char* kGameplayGpuParticlePresetsToPreload[] = {"hit_bomb_fire_core",
                                                                "hit_bomb_flash_core",
                                                                "hit_bomb_fire_mushroom",
                                                                "hit_bomb_fire_sparks",
                                                                "hit_bomb_golden_sparks",
                                                                "hit_bomb_black_smoke",
                                                                "hit_bomb_orange_smoke",
                                                                "hit_bomb_gray_smoke",

                                                                // 命中、撃破、運搬時の粒子。
                                                                "hit_damage_puni_mist",
                                                                "hit_damage_puni_splash",
                                                                "hit_damage_pop_stars",
                                                                "hit_damage_galaxy_streaks",
                                                                "hit_damage_galaxy_glints",
                                                                "hitfx_kickpunch_spark_particles",
                                                                "hitfx_kickpunch_thin_streaks",
                                                                "enemy_defeat_pop_smoke",
                                                                "enemy_defeat_smoke_trail",
                                                                "enemy_defeat_gold_stars",
                                                                "enemy_defeat_rainbow_twinkles",
                                                                "enemy_defeat_after_twinkle",
                                                                "hit_enemy_ability",
                                                                "hit_pull_bind",
                                                                "hit_pull_catch",
                                                                "hit_slime_elastic",
                                                                "player_base_bash_droplets",
                                                                "player_base_press_splash",
                                                                "hit_throw_slam_dust",
                                                                "carry_bomber_throw_sparks",
                                                                "carry_eye_charge_sparks",
                                                                "carry_eye_beam_sparks",
                                                                "carry_bat_glide_wisp",

                                                                // 雷・プリズム・火の敵攻撃と状態異常。
                                                                "thunder_slime_aura",
                                                                "thunder_slime_radial_charge",
                                                                "thunder_slime_charge",
                                                                "thunder_slime_discharge",
                                                                "thunder_slime_idle_spark",
                                                                "player_thunder_strike_impact",
                                                                "player_thunder_evade_sparks",
                                                                "prism_slime_charge",
                                                                "prism_slime_pulse",
                                                                "prism_spell_motes",
                                                                "prism_chant_glints",
                                                                "prism_lance_release",
                                                                "prism_spike_warning",
                                                                "prism_spike_burst",
                                                                "prism_spike_shatter",
                                                                "fire_slime_breath",
                                                                "fire_slime_breath_embers",
                                                                "fire_slime_cast",
                                                                "fireball_trail_flame",
                                                                "fireball_trail_embers",
                                                                "fireball_impact_flame",
                                                                "fireball_impact_sparks",
                                                                "fire_slime_head_flame",
                                                                "fire_slime_head_embers",
                                                                "status_burning_flame",
                                                                "hit_player_fire_flame",
                                                                "hit_player_fire_embers",
                                                                "hit_player_explosion_core",
                                                                "hit_player_explosion_sparks",
                                                                "hit_player_explosion_smoke",

                                                                // プレイヤーの移動、王冠、ゴール、各コピー能力。
                                                                "player_jump_dust",
                                                                "player_land_dust",
                                                                "crown_get_burst",
                                                                "crown_get_rays",
                                                                "crown_get_twinkle_fountain",
                                                                "crown_get_afterglow",
                                                                "crown_idle_twinkle",
                                                                "crown_idle_sparkle",
                                                                "crown_goal_idle_sparkle",
                                                                "goal_clear_gold_burst",
                                                                "goal_clear_gold_motes",
                                                                "goal_clear_silver_burst",
                                                                "goal_clear_silver_motes",
                                                                "player_thunder_discharge_charge",
                                                                "player_thunder_discharge_burst",
                                                                "player_fire_blaze_trail",
                                                                "player_fire_blaze_burst",
                                                                "player_pink_straight_splash",
                                                                "player_pink_bounce_droplets",
                                                                "pink_slime_morph_glint",
                                                                "player_bomb_place_fuse",
                                                                "player_bomb_blast_jump",
                                                                "player_bomb_morph_aura",
                                                                "player_fire_morph_aura",
                                                                "player_wind_morph_aura",
                                                                "player_wind_updraft",
                                                                "player_wind_dash",
                                                                "wind_slime_gust_impact",

                                                                // チェックポイント、記憶台、リングバーナー。
                                                                "checkpoint_active_motes",
                                                                "copy_memory_transfer_sparks",
                                                                "checkpoint_flag_capture_stars",
                                                                "ring_burner_charge_sparks",
                                                                "ring_burner_discharge_embers",

                                                                // マグマと偽王の多段攻撃。途中の演出段階も先読みします。
                                                                "magma_slime_core_embers",
                                                                "magma_slime_charge",
                                                                "magma_slime_mortar_trail",
                                                                "magma_slime_impact",
                                                                "magma_slime_rush_wake",
                                                                "magma_slime_rush_splash",
                                                                "magma_slime_slam_burst",
                                                                "magma_slime_geyser_burst",
                                                                "magma_slime_geyser_stream",
                                                                "magma_slime_spiral_surge",
                                                                "magma_slime_spiral_trail",
                                                                "magma_slime_fire_column_stream",
                                                                "magma_slime_flood_splash",
                                                                "false_king_charge",
                                                                "false_king_lance_trail",
                                                                "false_king_lance_impact",
                                                                "false_king_astral_reaver_charge",
                                                                "false_king_astral_reaver_trail",
                                                                "false_king_astral_reaver_impact",
                                                                "false_king_nightfall_lances_charge",
                                                                "false_king_nightfall_lances_trail",
                                                                "false_king_nightfall_lances_impact",
                                                                "false_king_rush_wake",
                                                                "false_king_shockwave",
                                                                "false_king_shockwave_charge_ring",
                                                                "false_king_dominion"};

// VFX Sequenceは個々の粒子・メッシュを束ねる発火順を持つため、素材とは別にロードします。
constexpr const char* kGameplayVfxSequencesToPreload[] = {"bomb_explosion_cue",
                                                          "player_base_bash_hit_cue",
                                                          "player_base_press_enemy_hit_cue",
                                                          "player_base_press_land_cue",
                                                          "player_pink_bounce_slam_cue",
                                                          "player_fire_dash_hit_cue",
                                                          "player_wind_soar_land_cue",
                                                          "damage_puni_burst_cue",
                                                          "enemy_defeat_pop_cue",
                                                          "slime_elastic_hit_cue",
                                                          "pull_bind_cue",
                                                          "pull_catch_cue",
                                                          "throw_slam_cue",
                                                          "enemy_ability_hit_cue",

                                                          // 王冠取得とゴールの色違いは個別シーケンスです。
                                                          "crown_get_cue",
                                                          "crown_focus_cue",
                                                          "crown_result_cue",
                                                          "crown_victory_land_cue",
                                                          "crown_focus_silver_cue",
                                                          "crown_get_silver_cue",
                                                          "crown_result_silver_cue",
                                                          "crown_victory_land_silver_cue",

                                                          // ボス部屋の封鎖から解放、チェックポイントまで。
                                                          "prism_arena_seal_cue",
                                                          "prism_midboss_appear_cue",
                                                          "prism_arena_release_cue",
                                                          "magma_arena_seal_cue",
                                                          "magma_midboss_appear_cue",
                                                          "magma_arena_release_cue",
                                                          "false_king_appear_cue",
                                                          "false_king_phase_shift_cue",
                                                          "false_king_dominion_cue",
                                                          "checkpoint_activate_cue",
                                                          "checkpoint_idle_cue",
                                                          "copy_memory_station_activate_cue"};

// 破片はGPU粒子とは別のランタイムで生成され、最初の衝突で待たないよう予熱します。
constexpr const char* kGameplayDebrisPresetsToPreload[] = {"bomb_hit_fragment_burst",
                                                           "prism_crystal_shatter",
                                                           "throw_slam_pebble_burst",
                                                           "pink_slime_charge_pebble_pull",
                                                           "pink_slime_landing_pebble_burst",
                                                           "rock_burst",
                                                           "small_pebble_scatter",
                                                           "wood_splinter_burst"};
} // namespace

// 非同期ロードは段階ごとに依存を満たしてから進めます。再入場時の状態復元はロード完了後に行います。

SceneLoadManifest GamePlayScene::BuildAsyncLoadManifest() const {
    // Scene Asset指定を優先し、通常のステージ入場だけStageManagerの配置へフォールバックします。
    // ここで列挙するのは「ロード画面中に読ませる」対象で、ゲーム中の使用可否ではありません。
    SceneLoadManifest manifest;

    std::string objectLayoutPath = "Resources/json/3Dobject/stage1.json";
    std::string spriteLayoutPath = "Resources/json/sprite/stage1_sprite.json";
    std::string skyboxPath = GetSceneLoadContext().skyboxPath;
    if (HasSceneAssetContext()) {
        if (!GetSceneLoadContext().objectLayoutPath.empty()) {
            objectLayoutPath = GetSceneLoadContext().objectLayoutPath;
        }
        if (!GetSceneLoadContext().spriteLayoutPath.empty()) {
            spriteLayoutPath = GetSceneLoadContext().spriteLayoutPath;
        }
    } else {
        const auto& stages = StageManager::GetInstance()->GetStages();
        const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
        if (stageIndex >= 0 && stageIndex < static_cast<int>(stages.size())) {
            objectLayoutPath = stages[stageIndex].levelPath;
            spriteLayoutPath = stages[stageIndex].spritePath;
            if (skyboxPath.empty()) {
                skyboxPath = stages[stageIndex].skyboxPath;
            }
        }
    }

    // 配置JSONと常設HUDは生成の前提です。ステージごとのSpriteは上で解決したパスを使います。
    manifest.AddObjectLayout(objectLayoutPath);
    manifest.AddSpriteLayout(spriteLayoutPath);
    manifest.AddSpriteLayout("Resources/json/sprite/gameplayHUD.json");
    manifest.AddSpriteLayout("Resources/json/sprite/controlsGuide.json");
    manifest.AddJson("Resources/json/animator/player_slime.json");
    manifest.AddJson("Resources/json/cinematic/goal_clear.json");
    // 実行時の管理器ごとに読込APIが違うため、同じ演出でも種類別にManifestへ登録します。
    for (const char* presetName : kGameplayGpuParticlePresetsToPreload) {
        manifest.AddJson("Resources/json/gpu_particles/" + std::string(presetName) + ".json");
    }
    for (const char* path : kGameplayMeshEffectsToPreload) {
        manifest.AddJson(path);
    }
    for (const char* presetName : kGameplayDebrisPresetsToPreload) {
        manifest.AddJson("Resources/json/debris/" + std::string(presetName) + ".json");
    }
    for (const char* sequenceName : kGameplayVfxSequencesToPreload) {
        manifest.AddJson("Resources/json/vfx_sequence/" + std::string(sequenceName) + ".json");
    }
    // 操作ガイドとコピー能力HUDは開始直後に開けるため、現在形態以外も先読みします。
    for (const char* path : kControlsGuidePortraitsToPreload) {
        manifest.AddTexture(path);
    }
    for (const char* path : kControlsGuideAbilityLabelsToPreload) {
        manifest.AddTexture(path);
    }
    for (const char* path : CopyStyleHud::kTexturePaths) {
        manifest.AddTexture("Resources/sprite/" + std::string(path));
    }
    // 演出カメラやゴール画面で使う固定素材は、Scene JSONに参照がなくても必要です。
    manifest.AddTexture("Resources/sprite/common/circle2.png");
    manifest.AddTexture("Resources/sprite/common/white.png");
    manifest.AddTexture("Resources/sprite/ui/hud/lockOn.png");
    manifest.AddTexture(skyboxPath.empty() ? "Resources/output_skybox.dds" : skyboxPath);
    manifest.AddTexture("Resources/sprite/particle/glow_core.png");
    manifest.AddTexture("Resources/sprite/particle/diamond_shard.png");
    manifest.AddTexture("Resources/sprite/effect/prism/prism_spell_circle.dds");
    manifest.AddTexture("Resources/sprite/effect/prism/prism_spike_ground_flash.dds");
    manifest.AddModel("Gimmicks/checkpoint_beacon_active");
    manifest.AddModel("Effects/prism_crystal_spike");
    manifest.AddModel("Effects/prism_crystal_fragment_a");
    manifest.AddModel("Effects/prism_crystal_fragment_b");
    manifest.AddModel("Effects/prism_crystal_fragment_c");
    manifest.AddTexture("Resources/sprite/fade/fade_sparkle.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/slime_letters/letter_s.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/slime_letters/letter_t.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/slime_letters/letter_a.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/slime_letters/letter_g.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/slime_letters/letter_e.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/slime_letters/letter_c.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/slime_letters/letter_l.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/slime_letters/letter_r.png");
    manifest.AddTexture("Resources/sprite/ui/result/clear/returning_select_text.png");
    // BGMはScene Assetの明示指定が最優先。空欄の通常ステージだけStageManagerへ問い合わせます。
    std::string preloadBgmPath = GetSceneLoadContext().bgmPath;
    if (preloadBgmPath.empty()) {
        const auto& preloadStages = StageManager::GetInstance()->GetStages();
        const int preloadStageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
        if (preloadStageIndex >= 0 && preloadStageIndex < static_cast<int>(preloadStages.size())) {
            preloadBgmPath = preloadStages[preloadStageIndex].bgmPath;
        }
    }
    if (!preloadBgmPath.empty()) {
        manifest.AddAudio(preloadBgmPath);
    }
    return manifest;
}

void GamePlayScene::Initialize() {
    // 同期起動時も段階ロードと同じ依存順を通す。
    BeginLoadingInitialize();
    while (!InitializeLoadingStep()) {
    }
}

void GamePlayScene::BeginLoadingInitialize() {
    // 再入場時にロード進捗と開始演出を両方戻します。演出側を残すと、
    // 新しい配置ができる前に前ステージの門・カメラを参照してしまいます。
    loadingInitializePhase_ = 0;
    loadingInitializeItemIndex_ = 0;
    loadingInitializeCompletedUnits_ = 0;
    loadingStageObjectsStarted_ = false;
    loadingRenderCommonPhase_ = 0;
#ifdef USE_IMGUI
    pendingDebugTeleportDestination_ = DebugTeleportDestination::None;
#endif
    stageEntryPresentationActive_ = false;
    stageEntryPresentationPending_ = true;
    stageEntryPresentationCompleted_ = false;
    stageEntryRuntimeWasPlaying_ = false;
    stageEntryPlayerEmergenceStarted_ = false;
    stageEntryPresentationTimer_ = 0.0f;
    stageEntryPresentationRetryTimer_ = 0.0f;
    stageEntryGate_ = nullptr;
    // 固定フェーズの完了通知13件と、実際に1件ずつ処理する素材数を足します。
    // フェーズ追加時はInitializeLoadingStepのcompleteUnitとの対応も更新が必要です。
    loadingInitializeTotalUnits_ =
        13 + std::size(kGameplayGpuParticlePresetsToPreload) + std::size(kGameplayMeshEffectsToPreload) +
        std::size(kGameplayDebrisPresetsToPreload) + std::size(kGameplayVfxSequencesToPreload);
}

bool GamePlayScene::InitializeLoadingStep() {
    // 1回の呼び出しで原則1単位だけ進め、ロード画面の描画機会を残します。
    // 配列を使い切った境界だけ同じ呼び出しで次のフェーズへ進むため、
    // 各caseのreturn/continueは進捗表示と依存順の両方に関わります。
    auto completeUnit = [this]() { ++loadingInitializeCompletedUnits_; };

    for (;;) {
        switch (loadingInitializePhase_) {
        // ステージの選択と描画基盤を先に確定します。後続のObject生成はこれらを借用します。
        case 0:
            if (HasSceneAssetContext()) {
                StageManager::GetInstance()->SetCurrentStageById(GetSceneLoadContext().sceneAssetId);
            }
            LoadGoalPresentationTuning();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 1:
            InitializeCoreSystems(StageManager::GetInstance()->GetCurrentStage());
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 2:
            if (!InitializeRenderCommons()) {
                return false;
            }
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 3:
            InitializeGameplayObjectSystems();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 4:
            // 管理器本体を初期化してから、各素材を1件ずつ登録します。
            InitializeGameplayParticleRuntime();
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            completeUnit();
            return false;
        case 5:
            if (loadingInitializeItemIndex_ < std::size(kGameplayGpuParticlePresetsToPreload)) {
                GPUParticleManager::GetInstance()->PreloadPresetSystem(
                    kGameplayGpuParticlePresetsToPreload[loadingInitializeItemIndex_++]);
                completeUnit();
                return false;
            }
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            continue;
        case 6:
            if (loadingInitializeItemIndex_ < std::size(kGameplayMeshEffectsToPreload)) {
                MeshEffectManager::GetInstance()->PreloadEffect(
                    kGameplayMeshEffectsToPreload[loadingInitializeItemIndex_++]);
                completeUnit();
                return false;
            }
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            continue;
        case 7:
            InitializeGameplayDebrisRuntime();
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            completeUnit();
            return false;
        case 8:
            if (loadingInitializeItemIndex_ < std::size(kGameplayDebrisPresetsToPreload)) {
                DebrisEffectManager::GetInstance()->PrewarmPreset(
                    kGameplayDebrisPresetsToPreload[loadingInitializeItemIndex_++]);
                completeUnit();
                return false;
            }
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            continue;
        case 9:
            if (loadingInitializeItemIndex_ < std::size(kGameplayVfxSequencesToPreload)) {
                VFXSequencer sequence;
                sequence.Load(kGameplayVfxSequencesToPreload[loadingInitializeItemIndex_++]);
                completeUnit();
                return false;
            }
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            continue;
        case 10:
            // Skyboxと実配置は先読みが済んでから作ります。
            InitializeGameplaySkybox();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 11:
            // 参照元となるランタイムと演出を準備してから、ステージの実オブジェクトを生成します。
            if (!loadingStageObjectsStarted_) {
                levelLoader_ = std::make_unique<LevelLoader>();
                levelLoader_->BeginObjectLayoutLoad(
                    this, StageManager::GetInstance()->GetCurrentStage().levelPath);
                loadingStageObjectsStarted_ = true;
            }
            // 配置 JSON 全件を一度に生成するとロード画面の描画が止まるため、毎フレーム数件ずつ復元する。
            if (!levelLoader_->ContinueObjectLayoutLoad(this, 4)) {
                return false;
            }
            ApplyGoalCrownState();
            ApplyStageStarCoinState();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 12:
            // Objectの生成完了後にSpriteとHUDを作ることで、Player参照とSceneの表示値を使えます。
            LoadCurrentStageSpritesAndView(StageManager::GetInstance()->GetCurrentStage());
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 13:
            InitializeGameplayHUD();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 14:
            InitializeGameplayOverlays();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 15: {
            // Scene Asset側の任意Controllerが未登録なら、操作不能を避けるためDEFAULTへ戻します。
            const std::string controllerName =
                GetSceneLoadContext().controllerName.empty() ? "DEFAULT" : GetSceneLoadContext().controllerName;
            sceneController_ = SceneControllerFactory::GetInstance()->Create(controllerName);
            if (!sceneController_) {
                LOG("Scene Controller not registered: " + controllerName + ". Falling back to DEFAULT.");
                sceneController_ = SceneControllerFactory::GetInstance()->Create("DEFAULT");
            }
            if (sceneController_) {
                sceneController_->OnInitialize(*this);
            }
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        }
        case 16:
            // 最後に演出タイムラインを接続し、完了をLoadingSceneへ返します。
            InitializeGoalCinematicTimeline();
            InitializeDebugAnimationPreview();
            ++loadingInitializePhase_;
            completeUnit();
            return true;
        default:
            return true;
        }
    }
}

float GamePlayScene::GetLoadingInitializeProgress() const {
    // まだ総作業数が設定されていない場合も 0 除算を避ける。
    if (loadingInitializeTotalUnits_ == 0) {
        return 1.0f;
    }
    const float renderProgress = loadingInitializePhase_ == 2
        ? (loadingRenderCommonPhase_ == 2 && object3dCommon_
            ? (1.0f + object3dCommon_->GetInitializationProgress()) / 3.0f
            : static_cast<float>(loadingRenderCommonPhase_) / 3.0f)
        : 0.0f;
    const float stageProgress = loadingInitializePhase_ == 11 && loadingStageObjectsStarted_ && levelLoader_
        ? levelLoader_->GetObjectLayoutLoadProgress()
        : 0.0f;
    return std::clamp((static_cast<float>(loadingInitializeCompletedUnits_) +
                           renderProgress + stageProgress) /
                          static_cast<float>(loadingInitializeTotalUnits_),
                      0.0f, 1.0f);
}

void GamePlayScene::OnActivated() {
    // 非同期ロード中はLoadingSceneも共有のライト状態を使用するため、
    // 現在シーンへ切り替わった時点でゲーム用の状態をもう一度確定します。
    ApplyGameplayRenderState(StageManager::GetInstance()->GetCurrentStage());
    BaseScene::OnActivated();

#ifdef USE_IMGUI
    // 編集停止中はdeltaTimeが0になるため、開始演出を再生するとカメラ固定が解除されません。
    // 残っている演出カメラも解除し、保存済みの自由カメラへ確実に戻します。
    SceneManager* sceneManager = GetSceneManager();
    if (sceneManager && !sceneManager->IsPlaying()) {
        if (stageEntryPresentationActive_) {
            FinishStageEntryPresentation();
        }

        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->EndOverride(0.0f);
            camera->SetInputEnabled(true);
            camera->SetFollowTarget(nullptr);
            camera->SetLockOnTarget(nullptr);
        }

        CameraEditor* cameraEditor = CameraEditor::GetInstance();
        cameraEditor->SetMode(CameraEditor::Mode::Editor);
        cameraEditor->Update(player_, false);
        stageEntryPresentationPending_ = true;
        stageEntryRuntimeWasPlaying_ = false;
        stageEntryPresentationRetryTimer_ = 0.0f;
        return;
    }
#endif

    stageEntryRuntimeWasPlaying_ = true;
    if (StartRespawnIrisInIfNeeded()) {
        stageEntryPresentationPending_ = false;
    } else {
        // 通常入場では前回の一時的な撃破状態を持ち越しません。
        GameDataManager::GetInstance()->ConsumeStageCheckpointRespawn();
        stageEntryHadPlayerControl_ = player_ ? player_->IsControlActive() : true;
        stageEntryPresentationPending_ = true;
        stageEntryPresentationRetryTimer_ = 0.0f;
    }
}

void GamePlayScene::Finalize() {
    // SceneController の通知を先に行い、その後に共有演出を含む資源を片付ける。
    if (sceneController_) {
        sceneController_->OnFinalize(*this);
        sceneController_.reset();
    }
    FinalizeGameplayResources();
}

void GamePlayScene::InitializeCoreSystems(const StageData& currentStage) {
    // シーン用のカメラ設定を初期化する。イベント購読は各所有者の破棄時に解除する。
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();

    LOG("Game Initialized!");

    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);
}

bool GamePlayScene::InitializeRenderCommons() {
    // 2D・3D・通常粒子の共通描画器を先に用意して各マネージャーへ渡す。
    if (loadingRenderCommonPhase_ == 0) {
        spriteCommon_ = std::make_unique<SpriteCommon>();
        spriteCommon_->Initialize(dxCommon_);
        ++loadingRenderCommonPhase_;
        return false;
    }
    if (loadingRenderCommonPhase_ == 1) {
        object3dCommon_ = std::make_unique<Object3dCommon>();
        object3dCommon_->BeginInitialize(dxCommon_);
        ++loadingRenderCommonPhase_;
        return false;
    }
    if (loadingRenderCommonPhase_ == 2) {
        if (!object3dCommon_->InitializeStep()) {
            return false;
        }
        MeshEffectManager::GetInstance()->Initialize(object3dCommon_.get());
        ++loadingRenderCommonPhase_;
        return false;
    }

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/common/circle2.png");

    ParticleManager::GetInstance()->Initialize(particleSystem_.get());

    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);

    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());
    ++loadingRenderCommonPhase_;
    return true;
}

void GamePlayScene::InitializeGameplaySystems() {
    // ゲーム中に使う演出を事前生成し、初回攻撃時の読み込み待ちを抑える。
    InitializeGameplayObjectSystems();
    InitializeGameplayParticleRuntime();
    for (const char* presetName : kGameplayGpuParticlePresetsToPreload) {
        GPUParticleManager::GetInstance()->PreloadPresetSystem(presetName);
    }
    for (const char* path : kGameplayMeshEffectsToPreload) {
        MeshEffectManager::GetInstance()->PreloadEffect(path);
    }
    InitializeGameplayDebrisRuntime();
    for (const char* presetName : kGameplayDebrisPresetsToPreload) {
        DebrisEffectManager::GetInstance()->PrewarmPreset(presetName);
    }
    for (const char* sequenceName : kGameplayVfxSequencesToPreload) {
        VFXSequencer sequence;
        sequence.Load(sequenceName);
    }
    InitializeGameplaySkybox();
}

void GamePlayScene::InitializeGameplayObjectSystems() {
    // オブジェクト管理・ロックオン・弾管理が同じシーンの入力と描画器を参照する。
    objectManager_ = std::make_unique<ObjectManager>();

    lockOnSystem_ = std::make_unique<LockOnSystem>();
    lockOnSystem_->Initialize(inputManager_);

    uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/ui/hud/lockOn.png");
    lockOnSprite_ = std::make_unique<Sprite>();
    lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
    lockOnSprite_->SetName("HUD_LockOn");
    lockOnSprite_->SetTextureName("ui/hud/lockOn.png");
    lockOnSprite_->SetAnchorPoint({0.5f, 0.5f});
    lockOnSprite_->SetSize({64.0f, 64.0f});
    lockOnSprite_->SetVisible(false);

    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());
}

void GamePlayScene::InitializeGameplayParticleRuntime() {
    // プリセット JSON を読み、シーン描画用の粒子テクスチャを確保する。
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
}

void GamePlayScene::InitializeGameplayDebrisRuntime() {
    // 破片演出を現在の 3D 描画器へ結び、プリセットを読み込む。
    DebrisEffectManager::GetInstance()->Initialize(object3dCommon_.get());
    DebrisEffectManager::GetInstance()->LoadAllPresets("Resources/json/debris/");
}

void GamePlayScene::InitializeGameplaySkybox() {
    // Scene Asset、ステージ定義、既定値の順で空のテクスチャ指定を補う。
    std::string skyboxPath = GetSceneLoadContext().skyboxPath;
    if (skyboxPath.empty()) {
        const StageData& stage = StageManager::GetInstance()->GetCurrentStage();
        skyboxPath = stage.skyboxPath;
    }
    if (skyboxPath.empty()) {
        skyboxPath = "Resources/output_skybox.dds";
    }

    skyboxTextureHandle_ = TextureManager::GetInstance()->Load(skyboxPath, TextureManager::TextureColorSpace::SRGB);
    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);
}

void GamePlayScene::ApplyGameplayRenderState(const StageData& currentStage) {
    // ロード画面と共有するライトを、ゲーム画面へ切り替わった時点で再適用する。
    const std::string defaultLightPath =
        currentStage.lightPath.empty() ? "Resources/json/light/light_layout.json" : currentStage.lightPath;

    LightManager* lightManager = LightManager::GetInstance();
    lightManager->LoadState(ResolveSceneLightPath(defaultLightPath));

    std::string skyboxPath = ResolveSceneSkyboxPath(currentStage.skyboxPath);
    if (!skyboxPath.empty() && !lightManager->SetSkyboxTexturePath(skyboxPath)) {
        lightManager->SetSkyboxTexturePath("Resources/output_skybox.dds");
    }

    skyboxTextureHandle_ = lightManager->GetSkyboxTextureHandle();
    if (skybox_) {
        skybox_->SetTextureHandle(skyboxTextureHandle_);
    }
}

void GamePlayScene::LoadCurrentStageContent(const StageData& currentStage) {
    // 3D 配置を先に読み、プレイヤー参照を必要とする HUD と Overlay を後で作る。
    LoadCurrentStageObjects(currentStage);
    LoadCurrentStageSpritesAndView(currentStage);
    InitializeGameplayHUD();
    InitializeGameplayOverlays();
}

void GamePlayScene::LoadCurrentStageObjects(const StageData& currentStage) {
    // 配置直後に進行記録を反映し、回収済みアイテムを再出現させない。
    levelLoader_ = std::make_unique<LevelLoader>();
    levelLoader_->LoadObjectLayout(this, currentStage.levelPath);
    ApplyGoalCrownState();
    ApplyStageStarCoinState();
}

void GamePlayScene::LoadCurrentStageSpritesAndView(const StageData& currentStage) {
    // ステージ固有の画面配置とカメラ JSON を同じロード段階で確定する。
    levelLoader_->LoadSpriteLayout(this, currentStage.spritePath);

    ApplyGameplayRenderState(currentStage);
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile(ResolveSceneCameraPath("game_camera.json"));
}

void GamePlayScene::InitializeGameplayOverlays() {
    // プレイヤー参照と SpriteCommon が揃ってから操作ガイドとメニューを作る。
    controlsGuideOverlay_ = std::make_unique<ControlsGuideOverlay>();
    controlsGuideOverlay_->Initialize(spriteCommon_.get(), player_);

    pauseMenuOverlay_ = std::make_unique<PauseMenuOverlay>();
    pauseMenuOverlay_->Initialize(spriteCommon_.get());

    settingsOverlay_ = std::make_unique<SettingsMenuOverlay>();
    settingsOverlay_->Initialize(spriteCommon_.get());

    saveIndicatorOverlay_ = std::make_unique<SaveIndicatorOverlay>();
    saveIndicatorOverlay_->Initialize(spriteCommon_.get());

    InitializeGoalPresentationOverlay();
}

void GamePlayScene::ApplyGoalCrownState() {
    // クリア済みのゴールは再取得できる輝きではなく、収集済みの見た目へ切り替える。
    if (!objectManager_) {
        return;
    }

    const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
    const bool isCleared = GameDataManager::GetInstance()->IsStageCleared(stageIndex);

    for (auto& object : objectManager_->GetObjects()) {
        if (!object || object->GetEventType() != EventType::Goal) {
            continue;
        }

        object->SetLodEnabled(false);
        object->SetGPUParticleName("");

        if (isCleared) {
            object->SetTexture("Resources/sprite/common/white.png");
            object->SetColor({0.48f, 0.52f, 0.58f, 1.0f});
            object->SetMetallic(0.38f);
            object->SetRoughness(0.72f);
            object->SetEnableEnvMap(false);
            object->SetEmissive(0.42f);
        } else {
            object->SetColor({1.0f, 0.78f, 0.22f, 1.0f});
            object->SetEmissive(1.65f);
        }
    }
}

void GamePlayScene::ApplyStageStarCoinState() {
    // 保存済みのスターコインを不可視にし、同じ位置で再取得できないようにする。
    if (!objectManager_) {
        return;
    }

    const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
    for (auto& object : objectManager_->GetObjects()) {
        if (!object || object->GetEventType() != EventType::StarCoin) {
            continue;
        }

        object->SetLodEnabled(false);
        const int starCoinIndex = object->GetTargetID();
        if (!GameDataManager::GetInstance()->IsStarCoinCollected(stageIndex, starCoinIndex)) {
            continue;
        }

        // 保存済みのスターコインは再配置せず、当たり判定も同時に無効化します。
        object->SetIsVisible(false);
        object->SetCollisionAttribute(0);
        object->SetCollisionMask(0);
    }
}

bool GamePlayScene::StartRespawnIrisInIfNeeded() {
    // 復活要求を一度だけ消費し、位置・コピー能力・クリア済み遭遇を戻してから開幕する。
    if (!GameDataManager::GetInstance()->ConsumeRespawnIrisInRequest()) {
        return false;
    }

    const GameDataManager::StageCheckpointRespawn checkpoint =
        GameDataManager::GetInstance()->ConsumeStageCheckpointRespawn();
    if (checkpoint.active && player_ && checkpoint.stageIndex == StageManager::GetInstance()->GetCurrentStageIndex()) {
        const Vector3 respawnPosition{checkpoint.positionX, checkpoint.positionY, checkpoint.positionZ};
        player_->SetTranslate(respawnPosition);
        player_->ActivateCheckpoint(respawnPosition, checkpoint.checkpointObjectName);
        if (gameRule_) {
            gameRule_->RestoreCheckpointPresentation(checkpoint.checkpointObjectName);
        }
        for (int id : checkpoint.clearedEncounterIds) {
            GameDataManager::GetInstance()->RememberCheckpointEncounter(checkpoint.stageIndex, id);
            if (auto* encounter = dynamic_cast<GimmickArenaEncounter*>(FindObjectByEventID(id)))
                encounter->RestoreCheckpointClear();
        }
        player_->SetVelocity({0.0f, 0.0f, 0.0f});

        if (!checkpoint.morphEnemyType.empty()) {
            // チェックポイントのコピー状態は敵を仮生成せず、保存情報から直接復元します。
            player_->ApplyStoredCopy(checkpoint.morphEnemyType, true);
        }
    }

    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    Vector2 irisCenter = {0.5f, 0.5f};
    if (cam && player_) {
        cam->Update();
        Vector3 worldPos = player_->GetWorldPosition();
        worldPos.y += 1.0f;
        Vector3 ndc = Math::Transform(worldPos, cam->GetViewProjectionMatrix());
        const bool isOnScreen = std::isfinite(ndc.x) && std::isfinite(ndc.y) && std::isfinite(ndc.z) && ndc.z >= 0.0f &&
                                std::abs(ndc.x) <= 1.15f && std::abs(ndc.y) <= 1.15f;
        if (isOnScreen) {
            irisCenter = {std::clamp((ndc.x + 1.0f) * 0.5f, 0.15f, 0.85f),
                          std::clamp((1.0f - ndc.y) * 0.5f, 0.12f, 0.88f)};
        }
    }

    Fade::GetInstance()->StartIrisIn(1.25f, irisCenter);
    return true;
}

void GamePlayScene::InitializeDebugAnimationPreview() {
    // 旧プレビューの初期化フック。現在のプレビューは独立した編集画面で管理する。
}

void GamePlayScene::FinalizeGameplayResources() {
    // カメラ・演出・当たり判定を先に解除し、シーンが所有する描画資源を後で破棄する。
    EndCrownMoviePreview();
    FinishSealBarrageMovie();
    ResetCrownDefeat();
    ResetCrownFinaleWorld();
    for (auto& message : crownFinaleMessages_)
        message.reset();
#ifdef USE_IMGUI
    ClearDebugPlayerMorph();
#endif
    if (stageEntryPresentationActive_) {
        FinishStageEntryPresentation();
    }
    if (arenaBossIntroActive_) {
        FinishArenaBossIntro();
    }
    if (arenaBossRewardActive_) {
        FinishArenaBossDefeatReward();
    }
    stageEntryPresentationPending_ = false;
    stageEntryRuntimeWasPlaying_ = false;
    stageEntryPresentationRetryTimer_ = 0.0f;
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetInputEnabled(true);
    }
    goalCinematicPlayer_.Stop(false);
    goalCinematicTimelineLoaded_ = false;
    RestoreGoalPresentationCameraInput();
    goalPresentationCamera_.reset();
    goalCameraSnapshotValid_ = false;
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    MeshEffectManager::GetInstance()->Clear();
    VFXSequencer::ClearOneShots();
    GPUParticleManager::GetInstance()->ClearSceneRuntime();
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();
    saveIndicatorOverlay_.reset();
    goalOverlayFlash_.reset();
    goalOverlayReturnText_.reset();
    for (auto& letter : goalOverlayStageClearLetters_) {
        letter.reset();
    }
    for (auto& sparkle : goalOverlaySparkles_) {
        sparkle.reset();
    }
    goalPresentationState_ = GoalPresentationState::Inactive;
    goalWasStageCleared_ = false;
    goalPresentationTimer_ = 0.0f;
    goalStarEmitTimer_ = 0.0f;
    goalBurstEmitTimer_ = 0.0f;
    goalPlayerSnapshotValid_ = false;
    goalCrownSnapshotValid_ = false;
    goalReturnFadeStarted_ = false;
    goalCrownObject_ = nullptr;
    goalClearPlayerAnimator_.Reset();
    controlsGuideOverlay_.reset();
    settingsOverlay_.reset();
    pauseMenuOverlay_.reset();
    hudHpIcon_ = {};
    hudHpFrame_ = {};
    hudHpDamageFill_ = {};
    hudHpFill_ = {};
    hudHpHighlight_ = {};
    hudHpAnimationTimer_ = 0.0f;
    hudLifeIcon_ = {};
    hudLifeXIcon_ = {};
    for (auto& digit : hudLifeDigits_) {
        digit = {};
    }
    hudCoinIcon_ = {};
    hudCoinXIcon_ = {};
    for (auto& digit : hudCoinDigits_) {
        digit = {};
    }
    for (auto& slot : hudStageStarSlots_) {
        slot = {};
    }
    hudStageStarFlyParticles_.clear();
    hudStageStarPulseTimers_ = {0.0f, 0.0f, 0.0f};
    hudStageStarVisualCollected_ = {false, false, false};
    hudLifeGainPulseTimer_ = 0.0f;
    hudCoinPulseTimer_ = 0.0f;
    hudPreviousLives_ = 0;
    hudPreviousCoins_ = 0;
    lifeLostIcon_ = {};
    lifeLostXIcon_ = {};
    for (auto& digit : lifeLostDigits_) {
        digit = {};
    }
    lifeLostBackdrop_ = {};
    lifeLostSlimeObject_.reset();
    lifeLostStunObject_.reset();
    lifeLostCamera_.reset();
    particleSystem_.reset();
    particleCommon_.reset();
    sprites_.clear();
    spriteCommon_.reset();
    object3dCommon_.reset();
    objectManager_.reset();
    lockOnSystem_.reset();
}

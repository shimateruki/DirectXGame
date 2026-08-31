#define NOMINMAX
#include "GamePlayScene.h"
#include "ScenePreloader.h"
#include "SceneController.h"
#include "SceneManager.h"

#include "AudioPlayer.h"
#include "BulletManager.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebrisEffectManager.h"
#include "DirectXCommon.h"
#include "EventManager.h"
#include "Fade.h"
#include "GameDataManager.h"
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
constexpr const char* kExplosionSePath = "Resources/audio/se/generated/explosion.wav";

constexpr const char* kControlsGuidePortraitsToPreload[] = {
    "Resources/sprite/ui/control_guide/portraits/pink_slime.png",
    "Resources/sprite/ui/control_guide/portraits/bomb_slime.png",
    "Resources/sprite/ui/control_guide/portraits/wind_slime.png",
    "Resources/sprite/ui/control_guide/portraits/fire_slime.png",
    "Resources/sprite/ui/control_guide/portraits/thunder_slime.png"
};

constexpr const char* kControlsGuideAbilityLabelsToPreload[] = {
    "Resources/sprite/ui/control_guide/labels/absorb.png",
    "Resources/sprite/ui/control_guide/labels/throw.png",
    "Resources/sprite/ui/control_guide/labels/slime_attack.png",
    "Resources/sprite/ui/control_guide/labels/hook_aim.png",
    "Resources/sprite/ui/control_guide/labels/slime_dive.png",
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
    "Resources/sprite/ui/control_guide/labels/wind_dash.png"
};

constexpr const char* kGameplayMeshEffectsToPreload[] = {
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
    "Resources/json/effect/effect_prism_spike_ground_flash.json",
    "Resources/json/effect/effect_prism_arena_seal_ring.json",
    "Resources/json/effect/effect_prism_midboss_summon_pillar.json",
    "Resources/json/effect/effect_prism_arena_release_wave.json",
    "Resources/json/effect/effect_enemy_defeat_core_flash.json",
    "Resources/json/effect/effect_enemy_defeat_pop_ring.json",
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
    "Resources/json/effect/effect_checkpoint_activate_pillar.json",
    "Resources/json/effect/effect_copy_memory_transfer_ring.json",
    "Resources/json/effect/effect_copy_memory_transfer_pillar.json",
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
    "Resources/json/effect/effect_ring_burner_charge.json",
    "Resources/json/effect/effect_ring_burner_wave.json",
    "Resources/json/effect/effect_ring_burner_wave_heat.json"
};

constexpr const char* kGameplayGpuParticlePresetsToPreload[] = {
    "hit_bomb_fire_core",
    "hit_bomb_flash_core",
    "hit_bomb_fire_mushroom",
    "hit_bomb_fire_sparks",
    "hit_bomb_golden_sparks",
    "hit_bomb_black_smoke",
    "hit_bomb_orange_smoke",
    "hit_bomb_gray_smoke",
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
    "thunder_slime_aura",
    "thunder_slime_radial_charge",
    "thunder_slime_charge",
    "thunder_slime_discharge",
    "thunder_slime_idle_spark",
    "player_thunder_strike_impact",
    "player_thunder_evade_sparks",
    "prism_slime_charge",
    "prism_slime_pulse",
    "prism_spike_warning",
    "prism_spike_burst",
    "prism_spike_shatter",
    "fire_slime_breath",
    "fire_slime_breath_embers",
    "fire_slime_cast",
    "fire_slime_head_flame",
    "fire_slime_head_embers",
    "status_burning_flame",
    "hit_player_fire_flame",
    "hit_player_fire_embers",
    "hit_player_explosion_core",
    "hit_player_explosion_sparks",
    "hit_player_explosion_smoke",
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
    "player_bomb_place_fuse",
    "player_bomb_blast_jump",
    "player_bomb_morph_aura",
    "player_fire_morph_aura",
    "player_wind_morph_aura",
    "player_wind_updraft",
    "player_wind_dash",
    "wind_slime_gust_impact",
    "checkpoint_active_motes",
    "copy_memory_transfer_sparks",
    "checkpoint_flag_capture_stars",
    "ring_burner_charge_sparks",
    "ring_burner_discharge_embers",
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
    "false_king_charge",
    "false_king_lance_trail",
    "false_king_lance_impact",
    "false_king_rush_wake",
    "false_king_shockwave",
    "false_king_dominion"
};

constexpr const char* kGameplayVfxSequencesToPreload[] = {
    "bomb_explosion_cue",
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
    "crown_get_cue",
    "crown_focus_cue",
    "crown_result_cue",
    "crown_victory_land_cue",
    "crown_focus_silver_cue",
    "crown_get_silver_cue",
    "crown_result_silver_cue",
    "crown_victory_land_silver_cue",
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
    "copy_memory_station_activate_cue"
};

constexpr const char* kGameplayDebrisPresetsToPreload[] = {
    "bomb_hit_fragment_burst",
    "prism_crystal_shatter",
    "throw_slam_pebble_burst",
    "pink_slime_charge_pebble_pull",
    "pink_slime_landing_pebble_burst",
    "rock_burst",
    "small_pebble_scatter",
    "wood_splinter_burst"
};
}

SceneLoadManifest GamePlayScene::BuildAsyncLoadManifest() const {
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
    }
    else {
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

    manifest.AddObjectLayout(objectLayoutPath);
    manifest.AddSpriteLayout(spriteLayoutPath);
    manifest.AddSpriteLayout("Resources/json/sprite/gameplayHUD.json");
    manifest.AddSpriteLayout("Resources/json/sprite/controlsGuide.json");
    manifest.AddJson("Resources/json/animator/player_slime.json");
    manifest.AddJson("Resources/json/cinematic/goal_clear.json");
    for (const char* presetName : kGameplayGpuParticlePresetsToPreload) {
        manifest.AddJson(
            "Resources/json/gpu_particles/" +
            std::string(presetName) +
            ".json");
    }
    for (const char* path : kGameplayMeshEffectsToPreload) {
        manifest.AddJson(path);
    }
    for (const char* presetName : kGameplayDebrisPresetsToPreload) {
        manifest.AddJson(
            "Resources/json/debris/" +
            std::string(presetName) +
            ".json");
    }
    for (const char* sequenceName : kGameplayVfxSequencesToPreload) {
        manifest.AddJson(
            "Resources/json/vfx_sequence/" +
            std::string(sequenceName) +
            ".json");
    }
    for (const char* path : kControlsGuidePortraitsToPreload) {
        manifest.AddTexture(path);
    }
    for (const char* path : kControlsGuideAbilityLabelsToPreload) {
        manifest.AddTexture(path);
    }
    manifest.AddTexture("Resources/sprite/common/circle2.png");
    manifest.AddTexture("Resources/sprite/common/white.png");
    manifest.AddTexture("Resources/sprite/ui/hud/lockOn.png");
    manifest.AddTexture(skyboxPath.empty()
        ? "Resources/output_skybox.dds"
        : skyboxPath);
    manifest.AddTexture("Resources/sprite/particle/glow_core.png");
    manifest.AddTexture("Resources/sprite/particle/diamond_shard.png");
    manifest.AddTexture("Resources/sprite/effect/prism/prism_spell_circle.dds");
    manifest.AddTexture("Resources/sprite/effect/prism/prism_spike_ground_flash.dds");
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
    std::string preloadBgmPath = GetSceneLoadContext().bgmPath;
    if (preloadBgmPath.empty()) {
        const auto& preloadStages = StageManager::GetInstance()->GetStages();
        const int preloadStageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
        if (preloadStageIndex >= 0 &&
            preloadStageIndex < static_cast<int>(preloadStages.size())) {
            preloadBgmPath = preloadStages[preloadStageIndex].bgmPath;
        }
    }
    if (!preloadBgmPath.empty()) {
        manifest.AddAudio(preloadBgmPath);
    }
    return manifest;
}

void GamePlayScene::Initialize() {
    BeginLoadingInitialize();
    while (!InitializeLoadingStep()) {
    }
}

void GamePlayScene::BeginLoadingInitialize() {
    loadingInitializePhase_ = 0;
    loadingInitializeItemIndex_ = 0;
    loadingInitializeCompletedUnits_ = 0;
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
    loadingInitializeTotalUnits_ =
        13 +
        std::size(kGameplayGpuParticlePresetsToPreload) +
        std::size(kGameplayMeshEffectsToPreload) +
        std::size(kGameplayDebrisPresetsToPreload) +
        std::size(kGameplayVfxSequencesToPreload);
}

bool GamePlayScene::InitializeLoadingStep() {
    auto completeUnit = [this]() {
        ++loadingInitializeCompletedUnits_;
    };

    for (;;) {
        switch (loadingInitializePhase_) {
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
            InitializeRenderCommons();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 3:
            InitializeGameplayObjectSystems();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 4:
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
            audioPlayer_->LoadSoundFile(kExplosionSePath);
            InitializeGameplaySkybox();
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 11:
            LoadCurrentStageObjects(StageManager::GetInstance()->GetCurrentStage());
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 12:
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
            const std::string controllerName = GetSceneLoadContext().controllerName.empty()
                ? "DEFAULT"
                : GetSceneLoadContext().controllerName;
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
    return loadingInitializeTotalUnits_ == 0
        ? 1.0f
        : std::clamp(
            static_cast<float>(loadingInitializeCompletedUnits_) /
                static_cast<float>(loadingInitializeTotalUnits_),
            0.0f,
            1.0f);
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
        stageEntryHadPlayerControl_ = player_ ? player_->IsControlActive() : true;
        stageEntryPresentationPending_ = true;
        stageEntryPresentationRetryTimer_ = 0.0f;
    }
}

void GamePlayScene::Finalize() {
    if (sceneController_) {
        sceneController_->OnFinalize(*this);
        sceneController_.reset();
    }
    FinalizeGameplayResources();
}

void GamePlayScene::InitializeCoreSystems(const StageData& currentStage) {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    LOG("Game Initialized!");
    bgmHandle_ = audioPlayer_->LoadSoundFile(ResolveSceneBgmPath(currentStage.bgmPath));

    EventManager::GetInstance()->ClearAllListeners();
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);
}

void GamePlayScene::InitializeRenderCommons() {
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);
    MeshEffectManager::GetInstance()->Initialize(object3dCommon_.get());

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/common/circle2.png");

    ParticleManager::GetInstance()->Initialize(particleSystem_.get());

    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);

    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());
}

void GamePlayScene::InitializeGameplaySystems() {
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
    audioPlayer_->LoadSoundFile(kExplosionSePath);
    InitializeGameplaySkybox();
}

void GamePlayScene::InitializeGameplayObjectSystems() {
    objectManager_ = std::make_unique<ObjectManager>();

    lockOnSystem_ = std::make_unique<LockOnSystem>();
    lockOnSystem_->Initialize(inputManager_);

    uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/ui/hud/lockOn.png");
    lockOnSprite_ = std::make_unique<Sprite>();
    lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
    lockOnSprite_->SetName("HUD_LockOn");
    lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f });
    lockOnSprite_->SetSize({ 64.0f, 64.0f });

    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());
}

void GamePlayScene::InitializeGameplayParticleRuntime() {
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
}

void GamePlayScene::InitializeGameplayDebrisRuntime() {
    DebrisEffectManager::GetInstance()->Initialize(object3dCommon_.get());
    DebrisEffectManager::GetInstance()->LoadAllPresets("Resources/json/debris/");
}

void GamePlayScene::InitializeGameplaySkybox() {
    std::string skyboxPath = GetSceneLoadContext().skyboxPath;
    if (skyboxPath.empty()) {
        const StageData& stage = StageManager::GetInstance()->GetCurrentStage();
        skyboxPath = stage.skyboxPath;
    }
    if (skyboxPath.empty()) {
        skyboxPath = "Resources/output_skybox.dds";
    }

    skyboxTextureHandle_ = TextureManager::GetInstance()->Load(
        skyboxPath, TextureManager::TextureColorSpace::SRGB);
    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);
}

void GamePlayScene::ApplyGameplayRenderState(const StageData& currentStage) {
    const std::string defaultLightPath = currentStage.lightPath.empty()
        ? "Resources/json/light/light_layout.json"
        : currentStage.lightPath;

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
    LoadCurrentStageObjects(currentStage);
    LoadCurrentStageSpritesAndView(currentStage);
    InitializeGameplayHUD();
    InitializeGameplayOverlays();
}

void GamePlayScene::LoadCurrentStageObjects(const StageData& currentStage) {
    levelLoader_ = std::make_unique<LevelLoader>();
    levelLoader_->LoadObjectLayout(this, currentStage.levelPath);
    ApplyGoalCrownState();
    ApplyStageStarCoinState();
}

void GamePlayScene::LoadCurrentStageSpritesAndView(const StageData& currentStage) {
    levelLoader_->LoadSpriteLayout(this, currentStage.spritePath);

    ApplyGameplayRenderState(currentStage);
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile(ResolveSceneCameraPath("game_camera.json"));
}

void GamePlayScene::InitializeGameplayOverlays() {
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
            object->SetColor({ 0.48f, 0.52f, 0.58f, 1.0f });
            object->SetMetallic(0.38f);
            object->SetRoughness(0.72f);
            object->SetEnableEnvMap(false);
            object->SetEmissive(0.42f);
        } else {
            object->SetColor({ 1.0f, 0.78f, 0.22f, 1.0f });
            object->SetEmissive(1.65f);
        }
    }
}

void GamePlayScene::ApplyStageStarCoinState() {
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
    if (!GameDataManager::GetInstance()->ConsumeRespawnIrisInRequest()) {
        return false;
    }

    const GameDataManager::StageCheckpointRespawn checkpoint =
        GameDataManager::GetInstance()->ConsumeStageCheckpointRespawn();
    if (checkpoint.active && player_ &&
        checkpoint.stageIndex == StageManager::GetInstance()->GetCurrentStageIndex()) {
        const Vector3 respawnPosition{
            checkpoint.positionX,
            checkpoint.positionY,
            checkpoint.positionZ
        };
        player_->SetTranslate(respawnPosition);
        player_->ActivateCheckpoint(respawnPosition);
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });

        if (!checkpoint.morphEnemyType.empty()) {
            // チェックポイントのコピー状態は敵を仮生成せず、保存情報から直接復元します。
            player_->ApplyStoredCopy(checkpoint.morphEnemyType, true);
        }
    }

    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    Vector2 irisCenter = { 0.5f, 0.5f };
    if (cam && player_) {
        cam->Update();
        Vector3 worldPos = player_->GetWorldPosition();
        worldPos.y += 1.0f;
        Vector3 ndc = Math::Transform(worldPos, cam->GetViewProjectionMatrix());
        const bool isOnScreen =
            std::isfinite(ndc.x) &&
            std::isfinite(ndc.y) &&
            std::isfinite(ndc.z) &&
            ndc.z >= 0.0f &&
            std::abs(ndc.x) <= 1.15f &&
            std::abs(ndc.y) <= 1.15f;
        if (isOnScreen) {
            irisCenter = {
                std::clamp((ndc.x + 1.0f) * 0.5f, 0.15f, 0.85f),
                std::clamp((1.0f - ndc.y) * 0.5f, 0.12f, 0.88f)
            };
        }
    }

    Fade::GetInstance()->StartIrisIn(1.25f, irisCenter);
    return true;
}

void GamePlayScene::InitializeDebugAnimationPreview() {

}

void GamePlayScene::FinalizeGameplayResources() {
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
    hudStageStarPulseTimers_ = { 0.0f, 0.0f, 0.0f };
    hudStageStarVisualCollected_ = { false, false, false };
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

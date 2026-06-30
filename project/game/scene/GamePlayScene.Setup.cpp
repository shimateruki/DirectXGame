#define NOMINMAX
#include "GamePlayScene.h"

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

namespace {
constexpr const char* kExplosionSePath = "Resources/audio/se/generated/explosion.wav";

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
    "Resources/json/effect/effect_warp_gate_pillar.json"
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
    "hit_throw_slam_dust",
    "carry_bomber_throw_sparks",
    "carry_eye_charge_sparks",
    "carry_eye_beam_sparks",
    "carry_bat_glide_wisp",
    "thunder_slime_aura",
    "thunder_slime_charge",
    "thunder_slime_discharge",
    "thunder_slime_idle_spark",
    "fire_slime_breath",
    "fire_slime_cast",
    "fire_slime_head_flame",
    "player_jump_dust",
    "player_land_dust",
    "crown_get_burst",
    "crown_get_rays",
    "crown_get_twinkle_fountain",
    "crown_get_afterglow",
    "crown_idle_sparkle"
};

constexpr const char* kGameplayVfxSequencesToPreload[] = {
    "bomb_explosion_cue",
    "damage_puni_burst_cue",
    "enemy_defeat_pop_cue",
    "slime_elastic_hit_cue",
    "pull_bind_cue",
    "pull_catch_cue",
    "throw_slam_cue",
    "enemy_ability_hit_cue",
    "crown_get_cue"
};

constexpr const char* kGameplayDebrisPresetsToPreload[] = {
    "bomb_hit_fragment_burst",
    "throw_slam_pebble_burst",
    "pink_slime_charge_pebble_pull",
    "pink_slime_landing_pebble_burst",
    "rock_burst",
    "small_pebble_scatter",
    "wood_splinter_burst"
};
}

void GamePlayScene::Initialize() {
    const StageData& currentStage = StageManager::GetInstance()->GetCurrentStage();

    InitializeCoreSystems(currentStage);
    InitializeRenderCommons();
    InitializeGameplaySystems();
    LoadCurrentStageContent(currentStage);
    StartRespawnIrisInIfNeeded();
    InitializeDebugAnimationPreview();

    dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {
    FinalizeGameplayResources();
}

void GamePlayScene::InitializeCoreSystems(const StageData& currentStage) {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    LOG("Game Initialized!");
    bgmHandle_ = audioPlayer_->LoadSoundFile(currentStage.bgmPath);

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
    objectManager_ = std::make_unique<ObjectManager>();

    lockOnSystem_ = std::make_unique<LockOnSystem>();
    lockOnSystem_->Initialize(inputManager_);

    uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/ui/hud/lockOn.png");
    lockOnSprite_ = std::make_unique<Sprite>();
    lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
    lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f });
    lockOnSprite_->SetSize({ 64.0f, 64.0f });

    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    for (const char* presetName : kGameplayGpuParticlePresetsToPreload) {
        GPUParticleManager::GetInstance()->PreloadPresetSystem(presetName);
    }

    for (const char* path : kGameplayMeshEffectsToPreload) {
        MeshEffectManager::GetInstance()->PreloadEffect(path);
    }
    DebrisEffectManager::GetInstance()->Initialize(object3dCommon_.get());
    DebrisEffectManager::GetInstance()->LoadAllPresets("Resources/json/debris/");
    for (const char* presetName : kGameplayDebrisPresetsToPreload) {
        DebrisEffectManager::GetInstance()->PrewarmPreset(presetName);
    }
    for (const char* sequenceName : kGameplayVfxSequencesToPreload) {
        VFXSequencer sequence;
        sequence.Load(sequenceName);
    }
    audioPlayer_->LoadSoundFile(kExplosionSePath);

    skyboxTextureHandle_ = TextureManager::GetInstance()->Load("Resources/output_skybox.dds");
    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);
}

void GamePlayScene::LoadCurrentStageContent(const StageData& currentStage) {
    levelLoader_ = std::make_unique<LevelLoader>();
    levelLoader_->LoadObjectLayout(this, currentStage.levelPath);
    levelLoader_->LoadSpriteLayout(this, currentStage.spritePath);

    LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("game_camera.json");

    InitializeGameplayHUD();

    pauseMenuOverlay_ = std::make_unique<PauseMenuOverlay>();
    pauseMenuOverlay_->Initialize(spriteCommon_.get());

    settingsOverlay_ = std::make_unique<SettingsMenuOverlay>();
    settingsOverlay_->Initialize(spriteCommon_.get());

    saveIndicatorOverlay_ = std::make_unique<SaveIndicatorOverlay>();
    saveIndicatorOverlay_->Initialize(spriteCommon_.get());

    InitializeGoalPresentationOverlay();
}

void GamePlayScene::StartRespawnIrisInIfNeeded() {
    if (!GameDataManager::GetInstance()->ConsumeRespawnIrisInRequest()) {
        return;
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
}

void GamePlayScene::InitializeDebugAnimationPreview() {

}

void GamePlayScene::FinalizeGameplayResources() {
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    MeshEffectManager::GetInstance()->Clear();
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();
    saveIndicatorOverlay_.reset();
    goalOverlayBackdrop_.reset();
    goalOverlayCrown_.reset();
    goalOverlayStageClearText_.reset();
    goalOverlayReturnText_.reset();
    goalPresentationState_ = GoalPresentationState::Inactive;
    goalPresentationTimer_ = 0.0f;
    goalStarEmitTimer_ = 0.0f;
    goalPlayerSnapshotValid_ = false;
    settingsOverlay_.reset();
    pauseMenuOverlay_.reset();
    hudHpIcon_ = {};
    hudHpFrame_ = {};
    hudHpDamageFill_ = {};
    hudHpFill_ = {};
    hudHpHighlight_ = {};
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

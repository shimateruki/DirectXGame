#define NOMINMAX
#include "GamePlayScene.h"

#include "AudioPlayer.h"
#include "BulletManager.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
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

#include <algorithm>
#include <cmath>

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
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

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
    animatedCube_ = std::make_unique<Object3d>();
    animatedCube_->Initialize(object3dCommon_.get());
    animatedCube_->SetModel("Samples/walk");

    if (animatedCube_->GetModel() && !animatedCube_->GetModel()->GetModelData().animations.empty()) {
        animatedCube_->animName_ = animatedCube_->GetModel()->GetModelData().animations[0].name;
    }

    animatedCube_->isAnimLoop_ = true;
    animatedCube_->SetTranslate({ 0.0f, 0.0f, 0.0f });
    animatedCube_->SetScale({ 2.0f, 2.0f, 2.0f });
}

void GamePlayScene::FinalizeGameplayResources() {
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();
    settingsOverlay_.reset();
    pauseMenuOverlay_.reset();
    hudLifeMeter_ = {};
    hudLifeMeterDigit_ = {};
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

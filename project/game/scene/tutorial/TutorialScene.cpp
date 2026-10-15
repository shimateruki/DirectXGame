#define NOMINMAX
#include "game/scene/tutorial/TutorialScene.h"
#include "game/system/GameSceneDrawing.h"
#include "ScenePreloader.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "AudioPlayer.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "ParticleSystem.h"
#include "imgui.h"
#include "LightManager.h"
#include <EventManager.h>
#include "SceneManager.h"
#include "DebugConsole.h"
#include "ProfilerManager.h"
#include "RenderStats.h"
#include <cassert>
#include "BulletManager.h"
#include "MoveStrategy3D.h"
#include "MoveStrategy2D.h"
#include "LevelLoader.h"
#include "LockOnSystem.h"
#include "GameRule.h"
#include "ObjectManager.h"
#include "DebrisEffectManager.h"
#include "MeshEffectManager.h"
#include "VFXSequencer.h"
#include "WinApp.h"
#include "IconsFontAwesome5.h"
#ifdef _DEBUG
#include "ParticleEditor.h"
#endif

// --- JSON (保存機能) ---
#include <fstream>
#include <string>
#include "json.hpp"
#include <numbers>
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include <BaseEnemy.h>
#include <game/actor/enemy/spawn/EnemyFactory.h>
#include <game/actor/enemy/spawn/EnemySpawner.h>
#include <LightEditor.h>
#include <ParticleManager.h>
#include <GPUParticleManager.h>
#include <SrvManager.h>
#include <PostEffect.h>
#include "StageManager.h"
#include "GameDataManager.h"
#include "GameSettingsManager.h"
#include "TutorialDirector.h"

#include <algorithm>
#include <iterator>

namespace {
constexpr const char* kTutorialMeshEffectsToPreload[] = {
    "Resources/json/effect/effect_pink_slime_charge_pulse_ring.json",
    "Resources/json/effect/effect_pink_slime_charge_core_flash.json",
    "Resources/json/effect/effect_pink_slime_charge_vortex_streak.json",
    "Resources/json/effect/effect_pink_slime_launch_kick_ring.json",
    "Resources/json/effect/effect_pink_slime_apex_focus_flash.json",
    "Resources/json/effect/effect_pink_slime_dive_streak.json",
    "Resources/json/effect/effect_pink_slime_landing_burst_ring.json",
    "Resources/json/effect/effect_pink_slime_landing_core_flash.json",
    "Resources/json/effect/effect_pink_slime_landing_shock_arc.json",
    "Resources/json/effect/effect_player_pink_straight_arc.json",
    "Resources/json/effect/effect_player_pink_straight_impact.json",
    "Resources/json/effect/effect_player_pink_bounce_launch.json",
    "Resources/json/effect/effect_player_pink_bounce_land.json",
    "Resources/json/effect/effect_player_base_bash_arc.json",
    "Resources/json/effect/effect_player_base_bash_impact.json",
    "Resources/json/effect/effect_player_base_press_impact.json",
    "Resources/json/effect/effect_player_pink_guard_start.json",
    "Resources/json/effect/effect_player_pink_guard_shell.json",
    "Resources/json/effect/effect_player_pink_guard_release.json"};

constexpr const char* kTutorialGpuParticlePresetsToPreload[] = {"hit_pull_bind",
                                                                "hit_pull_catch",
                                                                "hit_slime_elastic",
                                                                "player_base_bash_droplets",
                                                                "player_base_press_splash",
                                                                "hit_throw_slam_dust",
                                                                "hit_enemy_ability",
                                                                "player_jump_dust",
                                                                "player_land_dust",
                                                                "player_pink_straight_splash",
                                                                "player_pink_bounce_droplets"};

constexpr const char* kTutorialVfxSequencesToPreload[] = {"player_base_bash_hit_cue",
                                                          "player_base_press_enemy_hit_cue",
                                                          "player_base_press_land_cue",
                                                          "player_pink_bounce_slam_cue",
                                                          "slime_elastic_hit_cue",
                                                          "pull_bind_cue",
                                                          "pull_catch_cue",
                                                          "throw_slam_cue",
                                                          "enemy_ability_hit_cue"};

constexpr const char* kTutorialDebrisPresetsToPreload[] = {"throw_slam_pebble_burst", "pink_slime_charge_pebble_pull",
                                                           "pink_slime_landing_pebble_burst"};

constexpr const char* kTutorialPromptTexturesToPreload[] = {
    "Resources/sprite/ui/tutorial/tutorial_move.png",    "Resources/sprite/ui/tutorial/tutorial_look.png",
    "Resources/sprite/ui/tutorial/tutorial_jump.png",    "Resources/sprite/ui/tutorial/tutorial_lock_on.png",
    "Resources/sprite/ui/tutorial/tutorial_pull.png",    "Resources/sprite/ui/tutorial/tutorial_throw.png",
    "Resources/sprite/ui/tutorial/tutorial_absorb.png",  "Resources/sprite/ui/tutorial/tutorial_ability.png",
    "Resources/sprite/ui/tutorial/tutorial_release.png", "Resources/sprite/ui/tutorial/tutorial_controls.png",
    "Resources/sprite/ui/tutorial/tutorial_exit.png",    "Resources/sprite/ui/tutorial/tutorial_objective_marker.png"};
} // namespace

// チュートリアルのシーン側は入力・描画と手順管理の接続役です。課題の達成判定は TutorialDirector に委ねます。

TutorialScene::TutorialScene() {
    // 描画基盤の取得はシーン初期化まで遅らせる。
}
TutorialScene::~TutorialScene() {
    // シーン固有の解放はFinalizeへ集約する。
}

void TutorialScene::SetIsGoal(bool isGoal) {
    // 再挑戦などでゴール状態が解除されたら、保存済みフラグも戻す。
    if (isGoal_ == isGoal) {
        return;
    }

    isGoal_ = isGoal;
    if (!isGoal_) {
        goalSavePerformed_ = false;
        goalSaveErrorShown_ = false;
        goalSaveRetryTimer_ = 0.0f;
    }
}

void TutorialScene::CollectReplaySprites(std::vector<engine::Sprite*>& replaySprites) {
    // チュートリアル専用の案内と保存表示もリプレイの描画対象へ加える。
    BaseScene::CollectReplaySprites(replaySprites);
    if (lockOnSprite_) {
        replaySprites.push_back(lockOnSprite_.get());
    }
    if (controlsGuideOverlay_) {
        controlsGuideOverlay_->CollectReplaySprites(replaySprites);
    }
    if (saveIndicatorOverlay_) {
        saveIndicatorOverlay_->CollectReplaySprites(replaySprites);
    }
}

void TutorialScene::CaptureReplaySceneState(json& state) const {
    // シーン進行だけでなく、案内画面と保存演出の表示状態も記録する。
    json tutorialState = json::object();
    json controlsGuideState = json::object();
    json saveIndicatorState = json::object();
    if (tutorialDirector_)
        tutorialDirector_->CaptureReplayState(tutorialState);
    if (controlsGuideOverlay_)
        controlsGuideOverlay_->CaptureReplayState(controlsGuideState);
    if (saveIndicatorOverlay_)
        saveIndicatorOverlay_->CaptureReplayState(saveIndicatorState);

    state = {{"goal", {{"active", isGoal_}, {"savePerformed", goalSavePerformed_}}},
             {"movie",
              {{"state", static_cast<int>(movieState_)},
               {"timer", movieTimer_},
               {"startCameraEye", {movieStartCameraEye_.x, movieStartCameraEye_.y, movieStartCameraEye_.z}},
               {"startCameraTarget", {movieStartCameraTarget_.x, movieStartCameraTarget_.y, movieStartCameraTarget_.z}},
               {"bridgeDropped", hasBridgeDropped_}}},
             {"sessionStarCoins", {sessionStarCoins_[0], sessionStarCoins_[1], sessionStarCoins_[2]}},
             {"tutorial", std::move(tutorialState)},
             {"overlays",
              {{"controlsGuide", std::move(controlsGuideState)}, {"saveIndicator", std::move(saveIndicatorState)}}}};
}

void TutorialScene::RestoreReplaySceneState(const json& state) {
    // 任意項目を含む過去の記録でも復元できるよう、存在を確かめてから読む。
    if (!state.is_object()) {
        return;
    }

    auto restoreVector3 = [](const json& object, const char* key, engine::Vector3& value) {
        const auto found = object.find(key);
        if (found != object.end() && found->is_array() && found->size() >= 3) {
            value = {(*found)[0].get<float>(), (*found)[1].get<float>(), (*found)[2].get<float>()};
        }
    };

    if (const auto found = state.find("goal"); found != state.end() && found->is_object()) {
        isGoal_ = found->value("active", false);
        goalSavePerformed_ = found->value("savePerformed", false);
    }
    if (const auto found = state.find("movie"); found != state.end() && found->is_object()) {
        const int movieState = (std::clamp)(found->value("state", 0), 0, 1);
        movieState_ = static_cast<MovieState>(movieState);
        movieTimer_ = (std::max)(0.0f, found->value("timer", 0.0f));
        restoreVector3(*found, "startCameraEye", movieStartCameraEye_);
        restoreVector3(*found, "startCameraTarget", movieStartCameraTarget_);
        hasBridgeDropped_ = found->value("bridgeDropped", false);
    }
    if (const auto found = state.find("sessionStarCoins"); found != state.end() && found->is_array()) {
        for (size_t index = 0; index < 3 && index < found->size(); ++index) {
            sessionStarCoins_[index] = (*found)[index].get<bool>();
        }
    }
    if (tutorialDirector_ && state.contains("tutorial")) {
        tutorialDirector_->RestoreReplayState(state["tutorial"]);
    }
    if (const auto found = state.find("overlays"); found != state.end() && found->is_object()) {
        if (controlsGuideOverlay_ && found->contains("controlsGuide")) {
            controlsGuideOverlay_->RestoreReplayState((*found)["controlsGuide"]);
        }
        if (saveIndicatorOverlay_ && found->contains("saveIndicator")) {
            saveIndicatorOverlay_->RestoreReplayState((*found)["saveIndicator"]);
        }
    }
}

SceneLoadManifest TutorialScene::BuildAsyncLoadManifest() const {
    // 操作案内とプレイヤー演出を初回表示前に読み込み、描画中の遅延を抑える。
    SceneLoadManifest manifest;
    manifest.AddObjectLayout(HasSceneAssetContext() && !GetSceneLoadContext().objectLayoutPath.empty()
                                 ? GetSceneLoadContext().objectLayoutPath
                                 : "Resources/json/3Dobject/tutorial.json");
    manifest.AddSpriteLayout(HasSceneAssetContext() && !GetSceneLoadContext().spriteLayoutPath.empty()
                                 ? GetSceneLoadContext().spriteLayoutPath
                                 : "Resources/json/sprite/tutorialScene.json");
    manifest.AddSpriteLayout("Resources/json/sprite/controlsGuide.json");
    // 操作ガイドとコピー能力HUDの画像はcontrolsGuide.jsonの参照から収集します。
    manifest.AddTexture("Resources/sprite/common/circle2.png");
    manifest.AddTexture("Resources/sprite/common/white.png");
    manifest.AddTexture("Resources/sprite/ui/hud/lockOn.png");
    for (const char* path : kTutorialPromptTexturesToPreload) {
        manifest.AddTexture(path);
    }
    manifest.AddTexture(GetSceneLoadContext().skyboxPath.empty() ? "Resources/output_skybox.dds"
                                                                 : GetSceneLoadContext().skyboxPath);
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

void TutorialScene::Initialize() {
    // 同じ段階初期化を同期呼び出しでも使い、読込画面の有無で処理を分けない。
    BeginLoadingInitialize();
    while (!InitializeLoadingStep()) {
    }
}

void TutorialScene::BeginLoadingInitialize() {
    // 段階ごとの完了数を初期化し、読み込み進捗へ反映する。
    loadingInitializePhase_ = 0;
    loadingInitializeItemIndex_ = 0;
    loadingInitializeCompletedUnits_ = 0;
    loadingInitializeTotalUnits_ =
        10 + std::size(kTutorialGpuParticlePresetsToPreload) + std::size(kTutorialMeshEffectsToPreload) +
        std::size(kTutorialDebrisPresetsToPreload) + std::size(kTutorialVfxSequencesToPreload);
}

bool TutorialScene::InitializeLoadingStep() {
    // 一度の呼び出しでは一段階だけ進め、読込画面が途中経過を描けるようにする。
    auto completeUnit = [this]() { ++loadingInitializeCompletedUnits_; };

    for (;;) {
        switch (loadingInitializePhase_) {
        case 0: {
            dxCommon_ = DirectXCommon::GetInstance();
            inputManager_ = InputManager::GetInstance();
            LOG("Tutorial Scene Initialized!");
            const StageData& currentStage = StageManager::GetInstance()->GetCurrentStage();
            CameraManager::GetInstance()->Initialize();
            CameraManager::GetInstance()->SetInputManager(inputManager_);
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        }
        case 1:
            spriteCommon_ = std::make_unique<engine::SpriteCommon>();
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
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 2: {
            objectManager_ = std::make_unique<ObjectManager>();
            lockOnSystem_ = std::make_unique<LockOnSystem>();
            lockOnSystem_->Initialize(inputManager_);
            const uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/ui/hud/lockOn.png");
            lockOnSprite_ = std::make_unique<engine::Sprite>();
            lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
            lockOnSprite_->SetAnchorPoint({0.5f, 0.5f});
            lockOnSprite_->SetSize({64.0f, 64.0f});
            BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        }
        case 3:
            GPUParticleManager::GetInstance()->Initialize(dxCommon_);
            GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
            gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            completeUnit();
            return false;
        case 4:
            if (loadingInitializeItemIndex_ < std::size(kTutorialGpuParticlePresetsToPreload)) {
                GPUParticleManager::GetInstance()->PreloadPresetSystem(
                    kTutorialGpuParticlePresetsToPreload[loadingInitializeItemIndex_++]);
                completeUnit();
                return false;
            }
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            continue;
        case 5:
            if (loadingInitializeItemIndex_ < std::size(kTutorialMeshEffectsToPreload)) {
                MeshEffectManager::GetInstance()->PreloadEffect(
                    kTutorialMeshEffectsToPreload[loadingInitializeItemIndex_++]);
                completeUnit();
                return false;
            }
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            continue;
        case 6:
            DebrisEffectManager::GetInstance()->Initialize(object3dCommon_.get());
            DebrisEffectManager::GetInstance()->LoadAllPresets("Resources/json/debris/");
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            completeUnit();
            return false;
        case 7:
            if (loadingInitializeItemIndex_ < std::size(kTutorialDebrisPresetsToPreload)) {
                DebrisEffectManager::GetInstance()->PrewarmPreset(
                    kTutorialDebrisPresetsToPreload[loadingInitializeItemIndex_++]);
                completeUnit();
                return false;
            }
            ++loadingInitializePhase_;
            loadingInitializeItemIndex_ = 0;
            continue;
        case 8:
            if (loadingInitializeItemIndex_ < std::size(kTutorialVfxSequencesToPreload)) {
                VFXSequencer sequence;
                sequence.Load(kTutorialVfxSequencesToPreload[loadingInitializeItemIndex_++]);
                completeUnit();
                return false;
            }
            ++loadingInitializePhase_;
            continue;
        case 9:
            skyboxTextureHandle_ =
                TextureManager::GetInstance()->Load(ResolveSceneSkyboxPath("Resources/output_skybox.dds"));
            skybox_ = std::make_unique<Skybox>();
            skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 10:
            levelLoader_ = std::make_unique<LevelLoader>();
            levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/tutorial.json");
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 11:
            levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/tutorialScene.json");
            copyStyleHud_.Bind(GetSpriteByName("hud_copy_style"));
            if (player_) {
                player_->SetTutorialSafetyEnabled(true);
            }
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 12:
            controlsGuideOverlay_ = std::make_unique<ControlsGuideOverlay>();
            controlsGuideOverlay_->Initialize(spriteCommon_.get(), player_);
            saveIndicatorOverlay_ = std::make_unique<SaveIndicatorOverlay>();
            saveIndicatorOverlay_->Initialize(spriteCommon_.get());
            tutorialDirector_ = std::make_unique<TutorialDirector>();
            if (tutorialDirector_->Initialize(this, player_, inputManager_)) {
                tutorialDirector_->SetCompletionCallback([this]() { HandleTutorialFlowCompleted(); });
            }
            ++loadingInitializePhase_;
            completeUnit();
            return false;
        case 13:
            LightManager::GetInstance()->LoadState(ResolveSceneLightPath("Resources/json/light/light_layout.json"));
            CameraEditor::GetInstance()->Initialize();
            // 本編と同じ三人称カメラ設定を共有します。
            CameraEditor::GetInstance()->LoadFile("game_camera.json");
            CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                camera->ResetFollowSmoothing();
            }
            ++loadingInitializePhase_;
            completeUnit();
            return true;
        default:
            return true;
        }
    }
}

float TutorialScene::GetLoadingInitializeProgress() const {
    // 初期化前の分母ゼロは完了扱いにし、進捗表示を有効範囲に収める。
    return loadingInitializeTotalUnits_ == 0 ? 1.0f
                                             : std::clamp(static_cast<float>(loadingInitializeCompletedUnits_) /
                                                              static_cast<float>(loadingInitializeTotalUnits_),
                                                          0.0f, 1.0f);
}

void TutorialScene::Finalize() {
    // Directorを先に終了し、演出が参照する共有Managerをその後に片付ける。
    if (tutorialDirector_) {
        tutorialDirector_->Finalize();
    }
    tutorialDirector_.reset();
    if (controlsGuideOverlay_) {
        controlsGuideOverlay_->Finalize();
    }
    controlsGuideOverlay_.reset();
    MeshEffectManager::GetInstance()->Clear();
    VFXSequencer::ClearOneShots();
    GPUParticleManager::GetInstance()->ClearSceneRuntime();
    DebrisEffectManager::GetInstance()->Clear();
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();
    saveIndicatorOverlay_.reset();
    particleSystem_.reset();
    particleCommon_.reset();
    sprites_.clear();
    spriteCommon_.reset();
    object3dCommon_.reset();
    objectManager_.reset();
    lockOnSystem_.reset();
}

void TutorialScene::Update(float deltaTime) {
    // 手順完了後に保存できなければ出口を閉じたまま、一定間隔で再試行する。
    if (!isGoal_ && tutorialDirector_ && tutorialDirector_->IsCompleted() && !goalSavePerformed_) {
        goalSaveRetryTimer_ = (std::max)(0.0f, goalSaveRetryTimer_ - deltaTime);
        if (goalSaveRetryTimer_ <= 0.0f) HandleTutorialFlowCompleted();
    }
    // ゴール後はクリアを一度だけ保存し、保存演出が終わるまで選択画面へ進めない。
    if (isGoal_) {
        goalSaveRetryTimer_ = (std::max)(0.0f, goalSaveRetryTimer_ - deltaTime);
        if (!goalSavePerformed_ && goalSaveRetryTimer_ <= 0.0f) {
            if (GameDataManager::GetInstance()->CommitStageClear(-1, {})) {
                if (saveIndicatorOverlay_) saveIndicatorOverlay_->Play(1.35f);
                goalSavePerformed_ = true;
            } else {
                goalSaveRetryTimer_ = 5.0f;
                if (!goalSaveErrorShown_) {
                    ::MessageBoxW(nullptr, L"セーブに失敗しました。保存先を確認してください。5秒後に再試行します。",
                                  L"セーブエラー", MB_OK | MB_ICONERROR);
                    goalSaveErrorShown_ = true;
                }
            }
        }

        if (saveIndicatorOverlay_) {
            saveIndicatorOverlay_->Update(deltaTime);
        }

        deltaTime = 0.0f; // 時を止める

        if (goalSavePerformed_ && (inputManager_->IsKeyTriggered(DIK_SPACE) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) &&
            (!saveIndicatorOverlay_ || !saveIndicatorOverlay_->IsPlaying())) {
            SceneManager::GetInstance()->ChangeScene("SELECT");
            return;
        }
    }

    if (HandleControlsGuideOverlay(deltaTime)) {
        if (saveIndicatorOverlay_) {
            saveIndicatorOverlay_->Update(deltaTime);
        }
        return;
    }

    if (!isGoal_) {
        GameDataManager::GetInstance()->AdvancePlayTime(deltaTime);
    }

    // --- ポストエフェクト更新 ---
    PostEffect::GetInstance()->Update(deltaTime);
    Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
    if (activeCamera) {
        PostEffect::GetInstance()->GetParams()->projectionInverse = engine::Math::Inverse(activeCamera->GetProjectionMatrix());
    }

    static engine::Math math;
    LightEditor::GetInstance()->Update();

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_, deltaTime);
    CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());

    engine::Object3d* target = lockOnSystem_->GetTarget();

    if (target && lockOnSystem_->IsLockingOn()) {
        isDrawLockOn_ = true;
        AABB aabb = target->GetAABB();

        engine::Vector3 targetCenter;
        targetCenter.x = (aabb.min.x + aabb.max.x) * 0.5f;
        targetCenter.y = (aabb.min.y + aabb.max.y) * 0.5f;
        targetCenter.z = (aabb.min.z + aabb.max.z) * 0.5f;

        engine::Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
        float w = targetCenter.x * viewProj.m[0][3] + targetCenter.y * viewProj.m[1][3] +
                  targetCenter.z * viewProj.m[2][3] + viewProj.m[3][3];

        if (w > 0.001f) {
            engine::Vector3 ndc;
            ndc.x = (targetCenter.x * viewProj.m[0][0] + targetCenter.y * viewProj.m[1][0] +
                     targetCenter.z * viewProj.m[2][0] + viewProj.m[3][0]) /
                    w;
            ndc.y = (targetCenter.x * viewProj.m[0][1] + targetCenter.y * viewProj.m[1][1] +
                     targetCenter.z * viewProj.m[2][1] + viewProj.m[3][1]) /
                    w;

            float screenWidth = static_cast<float>(::WinApp::kClientWidth);
            float screenHeight = static_cast<float>(::WinApp::kClientHeight);
            float screenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
            float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;

            lockOnSprite_->SetPosition({screenX, screenY});

            float objSizeX = aabb.max.x - aabb.min.x;
            float objSizeY = aabb.max.y - aabb.min.y;
            float objSizeZ = aabb.max.z - aabb.min.z;
            float maxObjSize = std::max({objSizeX, objSizeY, objSizeZ});

            float baseSize = maxObjSize * 25.0f;
            float distanceScale = 20.0f / w;

            float finalSize = baseSize * distanceScale;
            finalSize = std::max(32.0f, std::min(finalSize, 256.0f));

            lockOnSprite_->SetSize({finalSize, finalSize});
            lockOnSprite_->SetRotation(lockOnSprite_->GetRotation() + 2.0f * deltaTime);
            lockOnSprite_->Update();
        } else {
            isDrawLockOn_ = false;
        }
    } else {
        isDrawLockOn_ = false;
    }

    if (!CameraEditor::GetInstance()->IsEditorMode()) {
        Camera::FollowMode currentMode = camera->GetFollowMode();

        if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
            camera->SetRotationSensitivity(GameSettingsManager::GetInstance()->GetCameraSensitivity());
            engine::Vector2 mouseDelta = inputManager_->GetCameraLookDelta(deltaTime);
            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
                camera->AddRotation(mouseDelta);
            }
        }
    }

    ProfilerManager::GetInstance()->SetObjectList(&objectManager_->GetObjects());
    CameraManager::GetInstance()->Update(deltaTime);
    particleSystem_->Update(deltaTime);
    if (tutorialDirector_) {
        tutorialDirector_->NotifyLockOnTarget(lockOnSystem_->IsLockingOn() ? lockOnSystem_->GetTarget() : nullptr);
        tutorialDirector_->Update(deltaTime);
    }
    objectManager_->Update(deltaTime);
    if (gameRule_) {
        gameRule_->Update(deltaTime);
    }
    GPUParticleManager::GetInstance()->Update(deltaTime);
    copyStyleHud_.Update(player_, deltaTime);
    for (auto& sprite : sprites_) {
        sprite->Update();
    }
    if (saveIndicatorOverlay_) {
        saveIndicatorOverlay_->Update(deltaTime);
    }
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
    UpdateUI();
}

void TutorialScene::Draw() {
    // カメラがプレイヤーに接近したら身体と子パーツを各3Dパスから除き、視界を確保します。
    // チュートリアルは専用の演出パスを持たず、通常モデル→半透明→深度参照表現の順です。
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

    if (player_ && camera) {
        engine::Vector3 pPos = player_->GetWorldPosition();
        pPos.y += 1.0f;
        engine::Vector3 cPos = camera->GetEye();
        engine::Vector3 toCam = {cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z};
        float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);

        if (dist < 3.0f) {
            isFirstPerson = true;
        }
    }

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();

    auto& objects = objectManager_->GetObjects();

    // 背景と不透明モデルで深度を確定してから、弾と半透明を描画します。
    if (skybox_ && camera && LightManager::GetInstance()->IsSkyboxEnabled()) {
        skybox_->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
        skybox_->Draw(camera->GetConstantBuffer());
    }

    object3dCommon_->SetGraphicsCommand();
    object3dCommon_->SetPipelineState(BlendMode::kNone);

    DrawSceneObjectPass(objects, MaterialRenderPolicy::Pass::Opaque, pointLightRes, spotLightRes,
        player_, isFirstPerson, true, [this](engine::Object3d* object) { return IsVisible(object); });

    // 身体を隠した近距離でも、操作の目印であるフックマーカーは残します。
    if (player_ && player_->GetHookMarker()) {
        player_->GetHookMarker()->Draw(pointLightRes, spotLightRes);
    }

    DrawEnemyTelegraphs(*this, pointLightRes, spotLightRes);
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    LightEditor::GetInstance()->Draw3D();

    // 半透明は不透明パスから外しておき、地形の後に重ねます。
    DrawSceneObjectPass(objects, MaterialRenderPolicy::Pass::Transparent, pointLightRes, spotLightRes,
        player_, isFirstPerson, true, {});
    particleSystem_->Draw();

    // 霧と特殊材質は画面色・深度を読むため、通常のObject描画とは分けます。
    DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);

    bool hasGPUParticles = !GPUParticleManager::GetInstance()->IsEmpty();
    bool grabUpdated =
        DrawSpecialMaterialObjects(objects, dxCommon_, BulletManager::GetInstance(), player_, isFirstPerson);
    if (hasGPUParticles) {
        DrawGPUParticles(dxCommon_, camera, gpuParticleTexHandle_, grabUpdated);
    }
}

void TutorialScene::DrawUI() {
    // 背景Sprite→照準→プレイヤーUI→操作案内→保存通知の順で重ねます。
    // 案内と保存通知はゲーム本体のSpriteより手前に置き、表示中に読めるようにします。
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    // チュートリアルのムービー中は画面上の操作UIを描かず、終了後に戻します。
    if (tutorialDirector_ && tutorialDirector_->IsCinematicPlaying()) {
        return;
    }
    for (auto& sprite : sprites_)
        sprite->Draw();
    if (isDrawLockOn_ && lockOnSprite_)
        lockOnSprite_->Draw();
    if (player_)
        player_->DrawUI();
    if (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) {
        controlsGuideOverlay_->Draw();
    }
    if (saveIndicatorOverlay_ && saveIndicatorOverlay_->IsActive())
        saveIndicatorOverlay_->Draw();
}

void TutorialScene::DrawShadow() {
    // 動的物体の影はObjectManagerに登録された描画対象から作る。
    if (objectManager_)
        objectManager_->DrawShadow();
}

void TutorialScene::UpdateUI() {
    // 専用のUI更新はUpdate内で行うため、共通フックでは追加処理をしない。
}

void TutorialScene::DrawImGui() {
#ifdef USE_IMGUI
    // 開発時だけ進行手順と保存演出を直接確認できるようにする。
    ImGui::Text(ICON_FA_INFO_CIRCLE " Scene: Tutorial");
    ImGui::Separator();
    if (ImGui::Button("Back to SELECT")) {
        SceneManager::GetInstance()->ChangeScene("SELECT");
    }
    if (saveIndicatorOverlay_ && ImGui::CollapsingHeader("Save Indicator", ImGuiTreeNodeFlags_DefaultOpen)) {
        saveIndicatorOverlay_->DrawImGui();
    }
    if (tutorialDirector_ && ImGui::CollapsingHeader("Tutorial Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
        tutorialDirector_->DrawImGui();
    }
    ImGui::Separator();
    ImGui::TextDisabled("※この項目は TutorialScene::DrawImGui() で編集可能です");
#endif
}

void TutorialScene::StartBridgeDropMovie() {
    // 橋の演出開始を固定の手順IDへ変換し、進行管理に委ねる。
    if (tutorialDirector_) {
        tutorialDirector_->JumpToStep("path_reveal");
    }
}

bool TutorialScene::HandleControlsGuideOverlay(float deltaTime) {
    // 操作案内を開いている間は通常ゲーム更新を止め、案内への入力を優先する。
    if (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) {
        controlsGuideOverlay_->SetPlayer(player_);
        controlsGuideOverlay_->Update(deltaTime);
        return true;
    }

    if (!IsControlsGuideOpenTriggered()) {
        return false;
    }

    if (controlsGuideOverlay_) {
        controlsGuideOverlay_->SetPlayer(player_);
        controlsGuideOverlay_->SetActive(true);
    }
    if (tutorialDirector_) {
        tutorialDirector_->NotifyControlsGuideOpened();
    }
    return true;
}

bool TutorialScene::IsControlsGuideOpenTriggered() const {
    // エディターがキー入力を受け取っている間は案内を開かない。
    if (!inputManager_ || isGoal_) {
        return false;
    }

#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard || io.WantTextInput) {
        return false;
    }
#endif

    return inputManager_->IsKeyTriggered(DIK_TAB) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_BACK);
}

void TutorialScene::HandleTutorialFlowCompleted() {
    // 進行達成による保存をゴール接触時の保存と重複させない。
    if (goalSavePerformed_) {
        return;
    }

    if (GameDataManager::GetInstance()->CommitStageClear(-1, {})) {
        if (tutorialDirector_) tutorialDirector_->SetCompletionGateEnabled(true);
        if (saveIndicatorOverlay_) saveIndicatorOverlay_->Play(1.35f);
        DebugConsole::GetInstance()->AddLog("Tutorial flow completed. Exit gate unlocked.");
        goalSavePerformed_ = true;
    } else {
        if (tutorialDirector_) tutorialDirector_->SetCompletionGateEnabled(false);
        goalSaveRetryTimer_ = 5.0f;
        if (!goalSaveErrorShown_) {
            ::MessageBoxW(nullptr, L"セーブに失敗しました。保存先を確認してください。ゴールで再試行します。",
                          L"セーブエラー", MB_OK | MB_ICONERROR);
            goalSaveErrorShown_ = true;
        }
    }
}

bool TutorialScene::IsVisible(engine::Object3d* obj) {
    return IsSceneObjectVisibleByFrustum(obj, CameraManager::GetInstance()->GetActiveCamera());
}

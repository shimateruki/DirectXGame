#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include "game/system/GameSceneDrawing.h"
#include "game/ui/PauseMenuOverlay.h"
#include "game/ui/SettingsMenuOverlay.h"
#include "ScenePreloader.h"

#include <algorithm>
#include <cmath>
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
#include <cassert>
#include "BulletManager.h"
#include "MoveStrategy3D.h"
#include "MoveStrategy2D.h"
#include "LevelLoader.h"
#include "LockOnSystem.h"
#include "GameRule.h"
#include "ObjectManager.h"
#include "MeshEffectManager.h"
#include "VFXSequencer.h"
#include "WinApp.h"
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
#include "GameAudioSettings.h"
#include "GameSettingsManager.h"
#include "GimmickStageGate.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <sstream>

#include "game/scene/select/GameSelectScene.Internal.h"
using namespace StageSelectDetail;

// ステージ選択のゲート表示は保存済み進行を基準に決めます。入場操作と解放演出を同じフレームで競合させません。

GameSelectScene::GameSelectScene() {
    // 共有システムの初期化は先読み段階まで遅らせる。
}
GameSelectScene::~GameSelectScene() {
    // シーン固有のリソース解放は Finalize にまとめる。
}

SceneLoadManifest GameSelectScene::BuildAsyncLoadManifest() const {
    // ゲート名とスターも先読みし、入場時の画像読み込み停止を避ける。
    SceneLoadManifest manifest;
    manifest.AddObjectLayout(HasSceneAssetContext() && !GetSceneLoadContext().objectLayoutPath.empty()
                                 ? GetSceneLoadContext().objectLayoutPath
                                 : "Resources/json/3Dobject/stageSelect.json");
    manifest.AddSpriteLayout(HasSceneAssetContext() && !GetSceneLoadContext().spriteLayoutPath.empty()
                                 ? GetSceneLoadContext().spriteLayoutPath
                                 : "Resources/json/sprite/stageSelect_sprite.json");
    manifest.AddTexture("Resources/sprite/common/circle2.png");
    manifest.AddTexture("Resources/sprite/common/white.png");
    manifest.AddTexture("Resources/sprite/ui/hud/lockOn.png");
    manifest.AddTexture("Resources/sprite/ui/gate/gate_prompt_player_e.png");
    manifest.AddTexture("Resources/sprite/ui/stage_select/stage1_gate_name.png");
    manifest.AddTexture("Resources/sprite/ui/stage_select/stage2_gate_name.png");
    manifest.AddTexture("Resources/sprite/ui/stage_select/stage3_gate_name.png");
    manifest.AddTexture("Resources/sprite/ui/hud/stage_star_filled.png");
    manifest.AddTexture("Resources/sprite/ui/hud/stage_star_empty.png");
    manifest.AddTexture(GetSceneLoadContext().skyboxPath.empty() ? "Resources/output_skybox.dds"
                                                                 : GetSceneLoadContext().skyboxPath);
    return manifest;
}

void GameSelectScene::Initialize() {
    // 同期初期化でも段階ロードと同じ順序を使用する。
    BeginLoadingInitialize();
    while (!InitializeLoadingStep()) {
    }
}

void GameSelectScene::BeginLoadingInitialize() {
    // 進捗表示用の作業数を、InitializeLoadingStep の段階数に合わせる。
    loadingInitializePhase_ = 0;
    loadingInitializeCompletedUnits_ = 0;
    loadingInitializeTotalUnits_ = 9;
}

bool GameSelectScene::InitializeLoadingStep() {
    // リソース生成を段階に分け、先読み時に一フレームへ集中させない。
    auto completeUnit = [this]() { ++loadingInitializeCompletedUnits_; };

    switch (loadingInitializePhase_) {
    case 0:
        dxCommon_ = DirectXCommon::GetInstance();
        inputManager_ = InputManager::GetInstance();
        LOG("Game Select Initialized!");
        StageManager::GetInstance()->Initialize();
        selectedStageIndex_ = StageManager::GetInstance()->GetCurrentStageIndex();
        if (selectedStageIndex_ < 0 ||
            selectedStageIndex_ >= static_cast<int>(StageManager::GetInstance()->GetStages().size())) {
            selectedStageIndex_ = 0;
        }
        if (GameDataManager::GetInstance()->IsStageCleared(selectedStageIndex_) &&
            IsStageUnlocked(selectedStageIndex_ + 1)) {
            ++selectedStageIndex_;
        }
        previousSelectedStageIndex_ = -1;
        stageDecisionCooldown_ = 0.0f;
        isChangingStage_ = false;
        unlockingStageIndex_ = -1;
        unlockPresentationTimer_ = 0.0f;
        unlockParticleTimer_ = 0.0f;
        unlockPresentationInitialized_ = false;
        unlockKeyAppearImpactPlayed_ = false;
        unlockTurnImpactPlayed_ = false;
        unlockReleaseImpactPlayed_ = false;
        unlockGateActivationImpactPlayed_ = false;
        unlockPresentationDebugReplay_ = false;
        unlockHadPlayerControl_ = true;
        unlockDebugPreviousSelectedStageIndex_ = -1;
        unlockTargetGate_ = nullptr;
        unlockKeyObject_ = nullptr;
        unlockLockObject_ = nullptr;
        stageSelectTime_ = 0.0f;
        crownCountPresentationActive_ = false;
        crownCountPresentationImpactDone_ = false;
        crownCountPresentationStageIndex_ = -1;
        crownCountPresentationFrom_ = 0;
        crownCountPresentationTo_ = 0;
        crownCountPresentationTimer_ = 0.0f;
        crownCountPresentationParticleTimer_ = 0.0f;
        gateReturnPresentationActive_ = false;
        gateReturnPresentationTimer_ = 0.0f;
        gateReturnStageIndex_ = -1;
        gateReturnTargetGate_ = nullptr;
        pendingStageClearRewardPresentation_ = {};
        ++loadingInitializePhase_;
        completeUnit();
        return false;
    case 1:
        CameraManager::GetInstance()->Initialize();
        CameraManager::GetInstance()->SetInputManager(inputManager_);
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
        ++loadingInitializePhase_;
        completeUnit();
        return false;
    case 2: {
        objectManager_ = std::make_unique<ObjectManager>();
        lockOnSystem_ = std::make_unique<LockOnSystem>();
        lockOnSystem_->Initialize(inputManager_);
        const uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/ui/hud/lockOn.png");
        lockOnSprite_ = std::make_unique<Sprite>();
        lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
        lockOnSprite_->SetAnchorPoint({0.5f, 0.5f});
        lockOnSprite_->SetSize({64.0f, 64.0f});
        gatePromptSprite_ = std::make_unique<Sprite>();
        gatePromptSprite_->Initialize(spriteCommon_.get(), "Resources/sprite/ui/gate/gate_prompt_player_e.png");
        gatePromptSprite_->SetAnchorPoint({0.5f, 0.5f});
        gatePromptSprite_->SetSize({192.0f, 64.0f});
        gatePromptSprite_->SetVisible(false);
        BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());
        ++loadingInitializePhase_;
        completeUnit();
        return false;
    }
    case 3:
        GPUParticleManager::GetInstance()->Initialize(dxCommon_);
        GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
        GPUParticleManager::GetInstance()->PreloadPresetSystems(
            {kCrownUnlockParticlePreset, kCrownUnlockAfterglowParticlePreset, kGateUnlockCoreParticlePreset,
             kGateUnlockSparkParticlePreset});
        gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
        ++loadingInitializePhase_;
        completeUnit();
        return false;
    case 4:
        skyboxTextureHandle_ =
            TextureManager::GetInstance()->Load(ResolveSceneSkyboxPath("Resources/output_skybox.dds"));
        skybox_ = std::make_unique<Skybox>();
        skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);
        ++loadingInitializePhase_;
        completeUnit();
        return false;
    case 5:
        levelLoader_ = std::make_unique<LevelLoader>();
        levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/stageSelect.json");
        // 旧形式のSceneでPlayerが静的保存されていても、床との衝突解決対象に戻します。
        if (player_) {
            player_->SetStatic(false);
        }
        ++loadingInitializePhase_;
        completeUnit();
        return false;
    case 6:
        levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/stageSelect_sprite.json");
        InitializeStageSelectHUD();
        InitializeStageGateNameLabels();
        pauseMenuOverlay_ = std::make_unique<PauseMenuOverlay>();
        pauseMenuOverlay_->Initialize(spriteCommon_.get(), "Resources/json/sprite/selectPauseMenu.json",
                                      PauseMenuOverlay::Mode::StageSelect);
        settingsOverlay_ = std::make_unique<SettingsMenuOverlay>();
        settingsOverlay_->Initialize(spriteCommon_.get());
        ++loadingInitializePhase_;
        completeUnit();
        return false;
    case 7:
        CameraEditor::GetInstance()->Initialize();
        CameraEditor::GetInstance()->LoadFile(ResolveSceneCameraPath("game_camera.json"));
        ++loadingInitializePhase_;
        completeUnit();
        return false;
    case 8: {
        auto* gameData = GameDataManager::GetInstance();
        gameData->MarkStageUnlockSeen(0);
        pendingStageClearRewardPresentation_ = gameData->ConsumeStageClearRewardPresentation();
        const bool startedReturnPresentation =
            StartStageReturnPresentation(gameData->ConsumeStageSelectReturnPresentation());
        if (!startedReturnPresentation) {
            StartStageClearRewardPresentation(pendingStageClearRewardPresentation_);
            pendingStageClearRewardPresentation_ = {};
        }
        unlockingStageIndex_ =
            (gateReturnPresentationActive_ || crownCountPresentationActive_) ? -1 : FindPendingUnlockStage();
        if (unlockingStageIndex_ >= 0) {
            selectedStageIndex_ = unlockingStageIndex_;
            DebugConsole::GetInstance()->AddLog("Stage Select: crown unlock presentation start.");
        }
        ApplyStageGateStates();
        UpdateStageSelectDecorations(0.0f);
        ++loadingInitializePhase_;
        completeUnit();
        return true;
    }
    default:
        return true;
    }
}

float GameSelectScene::GetLoadingInitializeProgress() const {
    // 段階ロード未開始時にも 0 除算せず完了値を返す。
    return loadingInitializeTotalUnits_ == 0 ? 1.0f
                                             : std::clamp(static_cast<float>(loadingInitializeCompletedUnits_) /
                                                              static_cast<float>(loadingInitializeTotalUnits_),
                                                          0.0f, 1.0f);
}

void GameSelectScene::OnActivated() {
    // 共有ライト状態は先読み中ではなく、ステージセレクトが有効になった時点で適用する。
    LightManager::GetInstance()->LoadState(ResolveSceneLightPath("Resources/json/light/stageSelect.json"));
    BaseScene::OnActivated();
}

void GameSelectScene::Finalize() {
    // 共有マネージャーのシーン固有状態を消してから所有リソースを解放する。
    MeshEffectManager::GetInstance()->Clear();
    VFXSequencer::ClearOneShots();
    GPUParticleManager::GetInstance()->ClearSceneRuntime();
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();
    particleSystem_.reset();
    particleCommon_.reset();
    sprites_.clear();
    stageGateNameLabels_.clear();
    stageGateStarIcons_.clear();
    if (settingsOverlay_) {
        settingsOverlay_->Finalize();
        settingsOverlay_.reset();
    }
    if (pauseMenuOverlay_) {
        pauseMenuOverlay_->Finalize();
        pauseMenuOverlay_.reset();
    }
    spriteCommon_.reset();
    object3dCommon_.reset();
    objectManager_.reset();
    lockOnSystem_.reset();
}

void GameSelectScene::Update(float deltaTime) {
    // ポーズ中はゲーム側の時計と衝突処理を進めず、オーバーレイだけ更新する。
    if (HandlePauseOverlay(deltaTime)) {
        return;
    }

    GameDataManager::GetInstance()->AdvancePlayTime(deltaTime);

    // --- ポストエフェクト更新 ---
    PostEffect::GetInstance()->Update(deltaTime);
    Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
    if (activeCamera) {
        PostEffect::GetInstance()->GetParams()->projectionInverse = Math::Inverse(activeCamera->GetProjectionMatrix());
    }

    static Math math;
    LightEditor::GetInstance()->Update();

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    // --- ロックオン & カメラ制御 ---
    lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_, deltaTime);
    if (unlockingStageIndex_ < 0) {
        CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());
    }

    // ロックオンアイコン更新
    Object3d* target = lockOnSystem_->GetTarget();
    if (target && lockOnSystem_->IsLockingOn()) {
        isDrawLockOn_ = true;
        AABB aabb = target->GetAABB();
        Vector3 targetCenter = (aabb.min + aabb.max) * 0.5f;
        Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
        float w = targetCenter.x * viewProj.m[0][3] + targetCenter.y * viewProj.m[1][3] +
                  targetCenter.z * viewProj.m[2][3] + viewProj.m[3][3];

        if (w > 0.001f) {
            Vector3 ndc;
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
            float finalSize = std::max(32.0f, std::min(baseSize * distanceScale, 256.0f));

            lockOnSprite_->SetSize({finalSize, finalSize});
            lockOnSprite_->SetRotation(lockOnSprite_->GetRotation() + 2.0f * deltaTime);
            lockOnSprite_->Update();
        } else {
            isDrawLockOn_ = false;
        }
    } else {
        isDrawLockOn_ = false;
    }

    // カメラ操作
    if (!CameraEditor::GetInstance()->IsEditorMode()) {
        Camera::FollowMode currentMode = camera->GetFollowMode();
        if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
            camera->SetRotationSensitivity(GameSettingsManager::GetInstance()->GetCameraSensitivity());
            Vector2 mouseDelta = inputManager_->GetCameraLookDelta(deltaTime);
            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)
                camera->AddRotation(mouseDelta);
        }
    }

    // --- 全体更新 ---
    if (gateEntryCinematicActive_) {
        UpdateGateEntryCinematic(deltaTime);
    }
    if (gateReturnPresentationActive_) {
        UpdateStageReturnPresentation(deltaTime);
    }

    CameraManager::GetInstance()->Update(deltaTime);
    particleSystem_->Update(deltaTime);
    const bool hadUnlockPresentation = unlockingStageIndex_ >= 0;
    UpdateStageGateSelection(deltaTime);
    objectManager_->Update(deltaTime);
    if (hadUnlockPresentation || unlockingStageIndex_ >= 0) {
        // 解放演出の固定カメラは通常カメラ更新後に設定されるため、
        // 同じフレームでもう一度更新して描画へ確実に反映します。
        CameraManager::GetInstance()->Update(0.0f);
    }
    if (gateReturnPresentationActive_) {
        UpdateStageReturnPresentation(0.0f);
        CameraManager::GetInstance()->Update(0.0f);
    }
    GPUParticleManager::GetInstance()->Update(deltaTime);
    for (auto& sprite : sprites_)
        sprite->Update();
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
    UpdateUI();
    UpdateStageGateNameLabels();
    UpdateGateCinematicBars(deltaTime);

    if (animatedCube_)
        animatedCube_->Update(deltaTime);
}

bool GameSelectScene::HandlePauseOverlay(float deltaTime) {
    // 解放・入場演出中にはポーズを開かず、ImGui の文字入力も奪わない。
    if (settingsOverlay_ && settingsOverlay_->IsActive()) {
        settingsOverlay_->Update(deltaTime);
        return true;
    }
    if (pauseMenuOverlay_ && pauseMenuOverlay_->IsActive()) {
        switch (pauseMenuOverlay_->Update(deltaTime)) {
        case PauseMenuOverlay::Action::Resume:
            pauseMenuOverlay_->SetActive(false);
            break;
        case PauseMenuOverlay::Action::OpenSettings:
            if (settingsOverlay_) {
                settingsOverlay_->SetActive(true);
            }
            break;
        case PauseMenuOverlay::Action::ReturnTitle:
            pauseMenuOverlay_->SetActive(false);
            SceneManager::GetInstance()->ChangeScene("TITLE");
            break;
        default:
            break;
        }
        return true;
    }
    if (!inputManager_ || isChangingStage_ || gateEntryCinematicActive_ || gateReturnPresentationActive_ ||
        crownCountPresentationActive_ || unlockingStageIndex_ >= 0) {
        return false;
    }
#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard || io.WantTextInput) {
        return false;
    }
#endif
    if (inputManager_->IsKeyTriggered(DIK_ESCAPE) || inputManager_->IsKeyTriggered(DIK_P) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_START)) {
        pauseMenuOverlay_->SetActive(true);
        return true;
    }
    return false;
}

void GameSelectScene::Draw() {
    // ゲートとプレイヤーを通常の3Dパスに描き、半透明・霧・特殊材質を後段へ回します。
    // 各パスで近距離のプレイヤーと子パーツを同じ条件で除外します。
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

    // FollowModeとは別に、壁際でカメラが胸元に寄った場合も身体を隠します。
    if (player_ && camera) {
        Vector3 pPos = player_->GetWorldPosition();
        pPos.y += 1.0f;
        Vector3 cPos = camera->GetEye();
        Vector3 toCam = {cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z};
        float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);
        if (dist < 3.0f)
            isFirstPerson = true;
    }

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();

    if (skybox_ && camera && LightManager::GetInstance()->IsSkyboxEnabled()) {
        skybox_->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
        skybox_->Draw(camera->GetConstantBuffer());
    }

    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    // 背景の後で不透明物の深度を先に決め、霧と特殊材質はここでは描きません。
    for (auto& obj : objects) {
        if (!obj->GetIsVisible())
            continue;
        bool isPlayerPart = false;
        if (isFirstPerson) {
            Object3d* current = obj.get();
            while (current) {
                if (current == player_) {
                    isPlayerPart = true;
                    break;
                }
                current = current->GetParent();
            }
        }
        if (isPlayerPart)
            continue;
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || IsSpecialMaterialType(obj->GetMaterialType()))
            continue;
        obj->Draw(pointLightRes, spotLightRes);
    }

    if (animatedCube_)
        animatedCube_->Draw(pointLightRes, spotLightRes);

    // ゲート等の不透明物が済んでから、弾・予兆・編集用3Dガイドを描きます。
    DrawEnemyTelegraphs(*this, pointLightRes, spotLightRes);
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    LightEditor::GetInstance()->Draw3D();

    // 半透明と通常粒子は不透明物の後に重ねます。
    for (auto& obj : objects) {
        if (!obj->GetIsVisible())
            continue;
        bool isPlayerPart = false;
        if (isFirstPerson) {
            Object3d* current = obj.get();
            while (current) {
                if (current == player_) {
                    isPlayerPart = true;
                    break;
                }
                current = current->GetParent();
            }
        }
        if (isPlayerPart)
            continue;
        if (obj->GetMaterialType() == 1)
            obj->Draw(pointLightRes, spotLightRes);
    }
    particleSystem_->Draw();

    // 霧の後、GrabTextureと深度を使う材質を描きます。GPU粒子はその画面コピーを再利用します。
    DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);

    const bool grabUpdated =
        DrawSpecialMaterialObjects(objects, dxCommon_, BulletManager::GetInstance(), player_, isFirstPerson);

    DrawGPUParticles(dxCommon_, camera, gpuParticleTexHandle_, grabUpdated);
}

void GameSelectScene::DrawUI() {
    // ゲートの名札と収集済みスターは、3Dの投影後に画面座標へ重ねます。
    // 操作Spriteと照準の後、設定画面をポーズより前面に出します。
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (auto& sprite : sprites_)
        sprite->Draw();
    for (auto& label : stageGateNameLabels_) {
        // JSON に名札のないステージのスロットは空なので、描画対象から除きます。
        if (label) {
            label->Draw();
        }
    }
    for (auto& stars : stageGateStarIcons_) {
        for (auto& star : stars) {
            if (star) {
                star->Draw();
            }
        }
    }
    if (gatePromptSprite_)
        gatePromptSprite_->Draw();
    if (isDrawLockOn_ && lockOnSprite_)
        lockOnSprite_->Draw();
    if (settingsOverlay_ && settingsOverlay_->IsActive()) {
        settingsOverlay_->Draw();
    } else if (pauseMenuOverlay_ && pauseMenuOverlay_->IsActive()) {
        pauseMenuOverlay_->Draw();
    }
}

void GameSelectScene::DrawShadow() {
    // 配置オブジェクトのうち、影を落とす対象の選別は ObjectManager に委ねる。
    if (objectManager_)
        objectManager_->DrawShadow();
}

void GameSelectScene::DrawImGui() {
    // 保存を変えずにゲート入場・解放演出を試せる調整 UI を提供する。
#ifdef USE_IMGUI
    ImGui::Text("ステージセレクト設定");
    ImGui::Separator();
    if (ImGui::CollapsingHeader("ゲート名札")) {
        ImGui::DragFloat("非表示になる距離 (m)", &gateNameHideDistance_, 0.5f, 5.0f, 100.0f);
        ImGui::DragFloat("フェード幅 (m)", &gateNameFadeDistance_, 0.5f, 1.0f, 40.0f);
        ImGui::DragFloat("ゲート上の余白 (m)", &gateNameHeightOffset_, 0.1f, 0.0f, 10.0f);
        ImGui::TextDisabled("橋へ近づくと消え、中央からは各ゲートの上に表示します。");
    }

    if (ImGui::CollapsingHeader("ゲート演出デバッグ", ImGuiTreeNodeFlags_DefaultOpen)) {
        float nearestDistance = std::numeric_limits<float>::max();
        Object3d* nearestGate = FindNearestStageGate(&nearestDistance);
        const int nearestGateStageIndex = GetStageGateIndex(nearestGate);
        const auto& stages = StageManager::GetInstance()->GetStages();
        const bool hasValidGate = player_ && nearestGate && nearestGateStageIndex >= 0 &&
                                  nearestGateStageIndex < static_cast<int>(stages.size());
        const bool canPlayDebug = hasValidGate && !gateEntryCinematicActive_ && !gateReturnPresentationActive_ &&
                                  !crownCountPresentationActive_ && unlockingStageIndex_ < 0;

        if (hasValidGate) {
            ImGui::Text("対象ゲート: Stage %d", nearestGateStageIndex + 1);
            ImGui::Text("プレイヤーからの距離: %.2f", nearestDistance);
            ImGui::TextDisabled("通常の接触判定を使わず、同じゲート突入演出を強制再生します。");
        } else {
            ImGui::TextDisabled("対象ゲート: なし");
            ImGui::TextDisabled("プレイヤーまたはステージゲートが見つからないため再生できません。");
        }

        ImGui::TextDisabled("ショートカット: G キー");

        const auto playDebugGateEntry = [&]() {
            selectedStageIndex_ = nearestGateStageIndex;
            StageManager::GetInstance()->SetCurrentStage(selectedStageIndex_);
            DebugConsole::GetInstance()->AddLog("Stage Select: debug gate entry cinematic.");
            StartGateEntryCinematic(selectedStageIndex_);
        };

        const bool shortcutPressed =
            canPlayDebug && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_G, false);

        if (shortcutPressed) {
            playDebugGateEntry();
        }

        if (!canPlayDebug) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("最寄りゲートで突入演出を強制再生", ImVec2(-1.0f, 34.0f))) {
            playDebugGateEntry();
        }
        if (!canPlayDebug) {
            ImGui::EndDisabled();
        }

        if (gateEntryCinematicActive_) {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "ゲート突入演出を再生中です。");
        }
    }

    if (ImGui::CollapsingHeader("王冠鍵 解放ムービーデバッグ", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& stages = StageManager::GetInstance()->GetStages();
        static int debugUnlockStageIndex = 1;
        const int maxUnlockStageIndex = static_cast<int>(stages.size()) - 1;
        debugUnlockStageIndex = std::clamp(debugUnlockStageIndex, 1, std::max(maxUnlockStageIndex, 1));

        if (maxUnlockStageIndex >= 1) {
            const std::string previewLabel = "Stage " + std::to_string(debugUnlockStageIndex + 1);
            if (ImGui::BeginCombo("再生対象", previewLabel.c_str())) {
                for (int stageIndex = 1; stageIndex <= maxUnlockStageIndex; ++stageIndex) {
                    const bool selected = stageIndex == debugUnlockStageIndex;
                    const std::string label = "Stage " + std::to_string(stageIndex + 1);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        debugUnlockStageIndex = stageIndex;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        Object3d* previewGate = maxUnlockStageIndex >= 1 ? FindStageGateByIndex(debugUnlockStageIndex) : nullptr;
        Object3d* previewKey =
            maxUnlockStageIndex >= 1
                ? FindObjectByName(std::string(kStageGateCrownKeyPrefix) + std::to_string(debugUnlockStageIndex))
                : nullptr;
        Object3d* previewLock =
            maxUnlockStageIndex >= 1
                ? FindObjectByName(std::string(kStageGateLockPrefix) + std::to_string(debugUnlockStageIndex))
                : nullptr;
        const bool anotherCinematicActive = gateEntryCinematicActive_ || gateReturnPresentationActive_ ||
                                            crownCountPresentationActive_ || unlockingStageIndex_ >= 0;
        const bool canPlayUnlockPreview =
            maxUnlockStageIndex >= 1 && previewGate && previewKey && previewLock && !anotherCinematicActive;

        ImGui::TextDisabled("セーブデータを変更せず、南京錠と王冠鍵を一時表示して再生します。");
        if (!previewGate || !previewKey || !previewLock) {
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f),
                               "対象ゲートの鍵・南京錠・ゲート本体が不足しています。");
        }
        if (!canPlayUnlockPreview) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("王冠鍵の解放ムービーを再生", ImVec2(-1.0f, 38.0f))) {
            StartDebugUnlockPresentation(debugUnlockStageIndex);
        }
        if (!canPlayUnlockPreview) {
            ImGui::EndDisabled();
        }

        if (unlockPresentationDebugReplay_) {
            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.28f, 1.0f), "王冠鍵の解放ムービーを再生中です。");
        }
    }
#endif
}

void GameSelectScene::UpdateStageGateSelection(float deltaTime) {
    // 解放・帰還・入場の演出中はゲート接触による別ステージへの遷移を止める。
    stageSelectTime_ += deltaTime;

    if (stageDecisionCooldown_ > 0.0f) {
        stageDecisionCooldown_ -= deltaTime;
        if (stageDecisionCooldown_ < 0.0f)
            stageDecisionCooldown_ = 0.0f;
    }

    UpdateStageClearRewardPresentation(deltaTime);
    UpdateUnlockPresentation(deltaTime);

    if (gateEntryCinematicActive_) {
        ApplyStageGateStates();
        UpdateStageSelectDecorations(deltaTime);
        UpdateGatePrompt(nullptr, false, deltaTime);
        return;
    }

    if (gateReturnPresentationActive_) {
        ApplyStageGateStates();
        UpdateStageSelectDecorations(deltaTime);
        UpdateGatePrompt(nullptr, false, deltaTime);
        return;
    }

    if (crownCountPresentationActive_ || unlockingStageIndex_ >= 0) {
        ApplyStageGateStates();
        UpdateStageSelectDecorations(deltaTime);
        UpdateGatePrompt(nullptr, false, deltaTime);
        return;
    }

    float nearestDistance = std::numeric_limits<float>::max();
    Object3d* nearestGate = FindNearestStageGate(&nearestDistance);
    bool canEnterGate = false;
    if (nearestGate && nearestDistance <= gateSelectRadius_) {
        int gateStageIndex = GetStageGateIndex(nearestGate);
        if (gateStageIndex >= 0 && gateStageIndex < static_cast<int>(StageManager::GetInstance()->GetStages().size())) {
            selectedStageIndex_ = gateStageIndex;
            canEnterGate = IsStageUnlocked(gateStageIndex);
        }
    }

    ApplyStageGateStates();
    UpdateStageSelectDecorations(deltaTime);
    UpdateGatePrompt(nullptr, false, deltaTime);

    if (selectedStageIndex_ != previousSelectedStageIndex_) {
        const auto& stages = StageManager::GetInstance()->GetStages();
        if (selectedStageIndex_ >= 0 && selectedStageIndex_ < static_cast<int>(stages.size())) {
            const StageData& stage = stages[selectedStageIndex_];
            DebugConsole::GetInstance()->AddLog("Stage Select: " + stage.name);
        }
        previousSelectedStageIndex_ = selectedStageIndex_;
    }

    const bool decide = canEnterGate && IsPlayerTouchingStageGate(nearestGate);

    if (decide) {
        EnterSelectedStage();
    }
}

void GameSelectScene::UpdateGatePrompt(Object3d* nearestGate, bool canEnterGate, float deltaTime) {
    // 入場可能なゲートが見えている時だけ、プレイヤー付近の画面内へ案内を置く。
    if (!gatePromptSprite_) {
        return;
    }

    if (!nearestGate || !canEnterGate || isChangingStage_ || !player_) {
        gatePromptSprite_->SetVisible(false);
        gatePromptSprite_->Update();
        return;
    }

    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) {
        gatePromptSprite_->SetVisible(false);
        gatePromptSprite_->Update();
        return;
    }

    Vector3 promptWorld = player_->GetWorldPosition();
    promptWorld.y += 1.35f;

    Math math;
    const Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    const Vector3 ndc = Math::Transform(promptWorld, viewProj);
    if (ndc.z < 0.0f || ndc.z > 1.0f) {
        gatePromptSprite_->SetVisible(false);
        gatePromptSprite_->Update();
        return;
    }

    const float screenWidth = static_cast<float>(WinApp::kClientWidth);
    const float screenHeight = static_cast<float>(WinApp::kClientHeight);
    const float playerScreenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
    const float playerScreenY = (1.0f - ndc.y) * 0.5f * screenHeight;
    const float baseWidth = 192.0f;
    const float baseHeight = 64.0f;
    float screenX = playerScreenX + 104.0f;
    float screenY = playerScreenY - 28.0f;

    gatePromptTimer_ += deltaTime;
    screenY += std::sin(gatePromptTimer_ * 4.0f) * 3.0f;
    if (screenX > screenWidth - baseWidth * 0.5f - 16.0f) {
        screenX = playerScreenX - 104.0f;
    }
    screenX = Math::Clamp(screenX, baseWidth * 0.5f + 12.0f, screenWidth - baseWidth * 0.5f - 12.0f);
    screenY = Math::Clamp(screenY, baseHeight * 0.5f + 12.0f, screenHeight - baseHeight * 0.5f - 18.0f);

    const float pulse = 1.0f + std::sin(gatePromptTimer_ * 5.5f) * 0.025f;
    gatePromptSprite_->SetVisible(true);
    gatePromptSprite_->SetPosition({screenX, screenY});
    gatePromptSprite_->SetSize({baseWidth * pulse, baseHeight * pulse});
    gatePromptSprite_->SetColor({1.0f, 1.0f, 1.0f, 0.98f});
    gatePromptSprite_->Update();
}

void GameSelectScene::ApplyStageGateStates() {
    // セーブの解放状態と演出進度を分離し、解放演出が終わるまで閉じた見た目を保つ。
    if (!objectManager_) {
        return;
    }

    float pulse = (std::sin(stageSelectTime_ * 5.0f) + 1.0f) * 0.5f;
    for (auto& object : objectManager_->GetObjects()) {
        if (!object || !IsStageGateObject(object.get())) {
            continue;
        }

        auto* gate = dynamic_cast<GimmickStageGate*>(object.get());
        int stageIndex = GetStageGateIndex(object.get());
        if (stageIndex < 0 || stageIndex >= static_cast<int>(StageManager::GetInstance()->GetStages().size())) {
            continue;
        }

        bool unlocked = IsStageUnlocked(stageIndex);
        bool cleared = GameDataManager::GetInstance()->IsStageCleared(stageIndex);
        bool unlocking = stageIndex == unlockingStageIndex_;
        bool selected = stageIndex == selectedStageIndex_;
        const bool unlockTimelineReady = unlocking && unlockPresentationInitialized_;
        const float gateUnlockT =
            unlockTimelineReady ? SelectEaseInOutCubic(SelectClamp01(
                                      (unlockPresentationTimer_ - kUnlockGateActivationStartTime) /
                                      std::max(kUnlockGateActivationEndTime - kUnlockGateActivationStartTime, 0.001f)))
                                : 0.0f;
        // カメラが正面へ到着するまでは、解放済みのセーブデータであっても閉じたゲートとして描画します。
        const bool gateVisualUnlocked =
            unlocking ? (unlockTimelineReady && unlockPresentationTimer_ >= kUnlockGateActivationStartTime) : unlocked;
        if (Object3d* lockSeal = FindObjectByName(std::string(kStageGateLockPrefix) + std::to_string(stageIndex))) {
            const bool showLock =
                unlocking ? (!unlockPresentationInitialized_ || unlockPresentationTimer_ < kUnlockLockHideTime)
                          : !unlocked;
            // 南京錠と鎖はゲートとは別サイズなので、Sceneで調整したTransformを維持します。
            lockSeal->SetIsVisible(showLock);
            const bool keyEngaged = unlockTimelineReady && unlockPresentationTimer_ >= kUnlockKeyApproachEndTime;
            lockSeal->SetColor(unlocking && keyEngaged ? Vector4{1.0f, 0.78f, 0.24f, 1.0f}
                                                       : (selected ? Vector4{0.86f, 0.68f, 0.34f, 1.0f}
                                                                   : Vector4{0.64f, 0.52f, 0.30f, 1.0f}));
            lockSeal->SetEmissive(unlocking && keyEngaged ? 1.45f + pulse * 0.75f : (selected ? 0.42f : 0.20f));
            lockSeal->SetCollisionAttribute(0);
            lockSeal->SetCollisionMask(0);
            lockSeal->SetColliderType(ColliderType::kNone);
        }
        if (Object3d* crownKey = FindObjectByName(std::string(kStageGateCrownKeyPrefix) + std::to_string(stageIndex))) {
            const bool showKey = unlockTimelineReady && unlockPresentationTimer_ >= kUnlockKeyAppearStartTime &&
                                 unlockPresentationTimer_ < kUnlockLockReleaseTime + 0.88f;
            crownKey->SetIsVisible(showKey);
            crownKey->SetCollisionAttribute(0);
            crownKey->SetCollisionMask(0);
            crownKey->SetColliderType(ColliderType::kNone);
        }
        float gateActivation = unlocking ? 1.0f : 0.0f;
        if (player_) {
            const Vector3 playerPos = player_->GetWorldPosition();
            const Vector3 gatePos = object->GetWorldPosition();
            const float dx = gatePos.x - playerPos.x;
            const float dz = gatePos.z - playerPos.z;
            const float distance = std::sqrt(dx * dx + dz * dz);
            const float nearRadius = gateSelectRadius_ * 0.55f;
            const float wakeRadius = gateSelectRadius_ * 1.55f;
            gateActivation =
                1.0f - Math::Clamp((distance - nearRadius) / std::max(wakeRadius - nearRadius, 0.001f), 0.0f, 1.0f);
            gateActivation = gateActivation * gateActivation * (3.0f - 2.0f * gateActivation);
            if (!unlocked) {
                gateActivation *= 0.18f;
            }
        }
        if ((gateEntryCinematicActive_ && object.get() == gateEntryTargetGate_) ||
            (gateReturnPresentationActive_ && object.get() == gateReturnTargetGate_)) {
            gateActivation = 1.0f;
            selected = true;
        }
        if (unlocking) {
            gateActivation = gateUnlockT;
            selected = true;
        }

        if (gate) {
            // GimmickStageGateのunlocking指定は即座に最大発光するため、演出では段階的なactivationを使います。
            gate->SetGateState(selected, gateVisualUnlocked, cleared, false);
            gate->SetGateActivation(gateActivation);
            continue;
        }

        object->SetIsVisible(true);
        object->SetCollisionAttribute(gateVisualUnlocked ? CollisionCategories::Get("trigger") : 0);
        object->SetCollisionMask(gateVisualUnlocked ? CollisionCategories::Get("player") : 0);
        object->SetColliderType(ColliderType::kSphere);
        object->SetCollisionRadius(gateSelectRadius_ * 0.55f);
        object->SetColor(GetStageGateModelColor(gateVisualUnlocked, selected, cleared, false, pulse));
        object->SetEmissive(GetStageGateModelEmissive(gateVisualUnlocked, selected, cleared, false, pulse));
    }
}

void GameSelectScene::RefreshDebugStageStates() {
    // デバッグで進行を変更したら、途中演出のキー・カメラ・操作権を元に戻す。
    const auto& stages = StageManager::GetInstance()->GetStages();
    if (stages.empty()) {
        return;
    }

    // デバッグ操作で進行状態を変更した場合も、途中の解放演出を確実に片付けます。
    if (unlockPresentationInitialized_) {
        if (unlockKeyObject_) {
            unlockKeyObject_->SetTranslate(unlockKeyBasePosition_);
            unlockKeyObject_->SetRotation(unlockKeyBaseRotation_);
            unlockKeyObject_->SetScale(unlockKeyBaseScale_);
            unlockKeyObject_->SetIsVisible(false);
            unlockKeyObject_->SetGPUParticleName("");
        }
        if (unlockLockObject_) {
            unlockLockObject_->SetTranslate(unlockLockBasePosition_);
            unlockLockObject_->SetRotation(unlockLockBaseRotation_);
            unlockLockObject_->SetScale(unlockLockBaseScale_);
        }
        if (player_) {
            player_->SetIsControlActive(unlockHadPlayerControl_);
            player_->SetVelocity({0.0f, 0.0f, 0.0f});
        }
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->SetEye(unlockCameraStartEye_);
            camera->SetTarget(unlockCameraStartTarget_);
            camera->SyncRotationToCurrentView();
            if (player_) {
                camera->SetFollowTarget(player_);
                camera->SetFollowMode(Camera::FollowMode::kAimable);
                camera->ResetFollowSmoothing();
            }
            camera->SetInputEnabled(true);
        }
    }
    if (unlockPresentationDebugReplay_ && unlockDebugPreviousSelectedStageIndex_ >= 0) {
        selectedStageIndex_ = unlockDebugPreviousSelectedStageIndex_;
    }
    unlockPresentationInitialized_ = false;
    unlockKeyAppearImpactPlayed_ = false;
    unlockTurnImpactPlayed_ = false;
    unlockReleaseImpactPlayed_ = false;
    unlockGateActivationImpactPlayed_ = false;
    unlockPresentationDebugReplay_ = false;
    unlockDebugPreviousSelectedStageIndex_ = -1;
    unlockTargetGate_ = nullptr;
    unlockKeyObject_ = nullptr;
    unlockLockObject_ = nullptr;

    if (selectedStageIndex_ < 0 || selectedStageIndex_ >= static_cast<int>(stages.size()) ||
        !IsStageUnlocked(selectedStageIndex_)) {
        selectedStageIndex_ = 0;
        for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
            if (IsStageUnlocked(stageIndex)) {
                selectedStageIndex_ = stageIndex;
                break;
            }
        }
    }

    previousSelectedStageIndex_ = -1;
    unlockingStageIndex_ = FindPendingUnlockStage();
    unlockPresentationTimer_ = 0.0f;
    unlockParticleTimer_ = 0.0f;
    ApplyStageGateStates();
    UpdateStageSelectDecorations(0.0f);
}

bool GameSelectScene::IsStageUnlocked(int stageIndex) const {
    // 初期解放か、前提ステージのクリア記録がある時だけ入場を許可する。
    const auto& stages = StageManager::GetInstance()->GetStages();
    if (stageIndex < 0 || stageIndex >= static_cast<int>(stages.size())) {
        return false;
    }

    const StageData& stage = stages[stageIndex];
    if (stage.defaultUnlocked || stageIndex == 0) {
        return true;
    }
    if (stage.unlockStageIndex < 0) {
        return false;
    }
    return GameDataManager::GetInstance()->IsStageCleared(stage.unlockStageIndex);
}

int GameSelectScene::FindPendingUnlockStage() const {
    // セーブ上は解放済みでも、解放演出をまだ見ていない最初のゲートを探す。
    const auto& stages = StageManager::GetInstance()->GetStages();
    auto* save = GameDataManager::GetInstance();

    for (int stageIndex = 1; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
        if (IsStageUnlocked(stageIndex) && !save->IsStageUnlockSeen(stageIndex)) {
            return stageIndex;
        }
    }
    return -1;
}

Object3d* GameSelectScene::FindObjectByName(const std::string& name) const {
    // 配置 JSON で名前を変更した演出部品は見つからないものとして扱う。
    if (!objectManager_) {
        return nullptr;
    }

    for (const auto& object : objectManager_->GetObjects()) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

Vector3 GameSelectScene::GetStageNodePosition(int stageIndex) const {
    // 実際に配置したゲート位置を優先し、未配置時だけ旧レイアウトの座標を使う。
    if (objectManager_) {
        for (const auto& object : objectManager_->GetObjects()) {
            if (object && IsStageGateObject(object.get()) && GetStageGateIndex(object.get()) == stageIndex) {
                return object->GetWorldPosition();
            }
        }
    }

    static const Vector3 fallbackPositions[] = {
        {-8.0f, 1.4f, 3.0f},
        {0.0f, 1.4f, -4.2f},
        {8.0f, 1.4f, 3.0f},
    };
    constexpr int fallbackCount = static_cast<int>(sizeof(fallbackPositions) / sizeof(fallbackPositions[0]));
    if (stageIndex >= 0 && stageIndex < fallbackCount) {
        return fallbackPositions[stageIndex];
    }
    return {0.0f, 1.4f, 0.0f};
}

void GameSelectScene::EnterSelectedStage() {
    // 二重入場を防ぎ、進行記録と選択ステージを確定してから入場演出を始める。
    if (isChangingStage_ || stageDecisionCooldown_ > 0.0f) {
        return;
    }

    const auto& stages = StageManager::GetInstance()->GetStages();
    if (selectedStageIndex_ < 0 || selectedStageIndex_ >= static_cast<int>(stages.size())) {
        return;
    }

    const StageData& stage = stages[selectedStageIndex_];
    if (!IsStageUnlocked(selectedStageIndex_)) {
        DebugConsole::GetInstance()->AddLog("Stage Select: " + stage.name + " is locked.");
        stageDecisionCooldown_ = 0.35f;
        return;
    }

    StageManager::GetInstance()->SetCurrentStage(selectedStageIndex_);
    DebugConsole::GetInstance()->AddLog("Stage Select: enter " + stage.name);
    StartGateEntryCinematic(selectedStageIndex_);
}

bool GameSelectScene::IsStageGateObject(const Object3d* object) const {
    // 新しい Gimmick 型と旧レイアウトの名前・モデル名の両方をゲートとして認識する。
    if (!object) {
        return false;
    }
    if (const auto* gate = dynamic_cast<const GimmickStageGate*>(object)) {
        return gate->IsStageSelectNodeMode();
    }
    if (object->GetGimmickType() == "StageGate") {
        if (!object->param_.has_value()) {
            return true;
        }
        return object->param_->actionMode == 0;
    }

    const std::string& modelName = object->GetModelName();
    const std::string& objectName = object->GetName();
    return modelName.find("portal_gate") != std::string::npos ||
           modelName.find("portal_surface") != std::string::npos ||
           objectName.find("portal_gate") != std::string::npos ||
           objectName.find("portal_surface") != std::string::npos || objectName.find("StageGate") != std::string::npos;
}

int GameSelectScene::GetStageGateIndex(const Object3d* object) const {
    // 新型は専用フィールド、旧型は汎用 TargetID からステージ番号を読む。
    if (!object) {
        return -1;
    }
    if (const auto* gate = dynamic_cast<const GimmickStageGate*>(object)) {
        return gate->GetStageIndex();
    }
    return object->GetTargetID();
}

Object3d* GameSelectScene::FindStageGateByIndex(int stageIndex) const {
    // 解放演出などが対象とするステージ番号のゲートを探す。
    if (!objectManager_) {
        return nullptr;
    }
    for (const auto& object : objectManager_->GetObjects()) {
        if (!object || !IsStageGateObject(object.get())) {
            continue;
        }
        if (GetStageGateIndex(object.get()) == stageIndex) {
            return object.get();
        }
    }
    return nullptr;
}

Object3d* GameSelectScene::FindNearestStageGate(float* outDistance) const {
    // 高低差ではなく水平距離で選び、画面上の近いゲートと選択を合わせる。
    if (outDistance) {
        *outDistance = std::numeric_limits<float>::max();
    }
    if (!objectManager_ || !player_) {
        return nullptr;
    }

    const Vector3 playerPos = player_->GetWorldPosition();
    Object3d* nearestGate = nullptr;
    float nearestDistanceSq = std::numeric_limits<float>::max();

    for (const auto& object : objectManager_->GetObjects()) {
        if (!object || !IsStageGateObject(object.get())) {
            continue;
        }
        if (GetStageGateIndex(object.get()) < 0) {
            continue;
        }

        const Vector3 gatePos = object->GetWorldPosition();
        float dx = gatePos.x - playerPos.x;
        float dz = gatePos.z - playerPos.z;
        float distanceSq = dx * dx + dz * dz;
        if (distanceSq < nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            nearestGate = object.get();
        }
    }

    if (outDistance && nearestGate) {
        *outDistance = std::sqrt(nearestDistanceSq);
    }
    return nearestGate;
}

bool GameSelectScene::IsPlayerTouchingStageGate(Object3d* gate) const {
    // 門の向きに沿う薄い平面判定を使い、旧配置の X/Z 軸向きも許容する。
    if (!gate || !player_) {
        return false;
    }

    const Vector3 gatePos = gate->GetWorldPosition();
    const Vector3 playerPos = player_->GetWorldPosition();
    const Vector3 rel = {playerPos.x - gatePos.x, 0.0f, playerPos.z - gatePos.z};
    const float distanceSq = rel.x * rel.x + rel.z * rel.z;

    const Vector3 gateScale = gate->GetScale();
    const float gateSize = std::max(1.0f, std::max(std::fabs(gateScale.x), std::fabs(gateScale.z)));
    const float halfWidth = std::clamp(gateSize * 0.36f, kGateEntryMinHalfWidth, kGateEntryMaxHalfWidth);
    const float planeThickness =
        std::clamp(gateSize * 0.045f, kGateEntryMinPlaneThickness, kGateEntryMaxPlaneThickness) +
        kGateEntryPlayerTouchRadius;

    float yaw = 0.0f;
    if (const Transform* transform = gate->GetTransform()) {
        yaw = transform->rotate.y;
    }

    const Vector3 forward = {std::sin(yaw), 0.0f, std::cos(yaw)};
    const Vector3 right = {std::cos(yaw), 0.0f, -std::sin(yaw)};
    const float planeDistance = rel.x * forward.x + rel.z * forward.z;
    const float sideDistance = rel.x * right.x + rel.z * right.z;
    const bool yawPlaneTouch = std::fabs(planeDistance) <= planeThickness && std::fabs(sideDistance) <= halfWidth;

    const bool xPlaneTouch = std::fabs(rel.x) <= planeThickness && std::fabs(rel.z) <= halfWidth;
    const bool zPlaneTouch = std::fabs(rel.z) <= planeThickness && std::fabs(rel.x) <= halfWidth;
    return yawPlaneTouch || xPlaneTouch || zPlaneTouch;
}

#define NOMINMAX
#include "TitleScene.h"
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
#include "SceneManager.h"
#include "DebugConsole.h"
#include "BulletManager.h"
#include "LevelLoader.h"
#include "GameRule.h"
#include "CameraEditor.h"
#include "LightEditor.h"
#include "ParticleManager.h"
#include "GPUParticleManager.h"
#include "GameDataManager.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>

void TitleScene::Initialize() {
    // --- 1. システム基盤の取得 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 2. モデルのプリロード ---
    ModelManager::GetInstance()->LoadModel("Characters/player");
    ModelManager::GetInstance()->LoadModel("Samples/teapot");
    LOG("TitleScene Initialized!");

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

    // --- 3. マネージャ・共通クラスの初期化 ---
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/common/white.png");

    // シングルトンのParticleManagerに今のシーンのシステムを紐づける
    ParticleManager::GetInstance()->Initialize(particleSystem_.get());

    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    // --- 4. サブシステムの生成 ---
    objectManager_ = std::make_unique<ObjectManager>();
    levelLoader_ = std::make_unique<LevelLoader>();
    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);

    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

    //  GPUパーティクルの初期化
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");



    // --- 6. レイアウトの読み込み (LevelLoaderへ委譲) ---
    levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/titleScene.json");
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/titleScene.json");

    LightManager::GetInstance()->LoadState("Resources/json/light/titleScene.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("title_camera.json");
    
    startTextSprite_ = GetSpriteByName("gameStartText.png");
    settingTextSprite_ = GetSpriteByName("setting.png");
    InitializeSaveSlotUI();
    UpdateSaveSlotUI();

    dxCommon_->FlushCommandQueue(false);
}

void TitleScene::Finalize() {
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();

    objectManager_.reset();
    titleUiSprites_.clear();
    saveSlotCards_.fill(nullptr);
    saveSlotIcons_.fill(nullptr);
    for (auto& dots : saveSlotProgressDots_) {
        dots.fill(nullptr);
    }
    saveSelectHeader_ = nullptr;
    sprites_.clear();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void TitleScene::Update(float deltaTime) {
    titleUiTime_ += deltaTime;

    if (titleMode_ == TitleMode::MainMenu) {
        UpdateMainMenu();
    } else {
        UpdateSaveSelect();
    }

    UpdateSaveSlotUI();

    // 常に実行されるマネージャ更新
    LightEditor::GetInstance()->Update();
    CameraManager::GetInstance()->Update();
    CameraEditor::GetInstance()->Update(player_, false);

    // オブジェクト一括更新 (ObjectManagerに委譲)
    objectManager_->Update(deltaTime);
    particleSystem_->Update(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }
    for (auto& sprite : titleUiSprites_) {
        sprite->Update();
    }

    // 各種グローバル更新
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
}

void TitleScene::UpdateMainMenu() {
    InputManager* input = InputManager::GetInstance();

    if (input->IsKeyTriggered(DIK_UP) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP)) {
        currentMenuIndex_--;
        if (currentMenuIndex_ < 0) currentMenuIndex_ = (int)MenuIndex::Max - 1;
    }
    if (input->IsKeyTriggered(DIK_DOWN) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN)) {
        currentMenuIndex_++;
        if (currentMenuIndex_ >= (int)MenuIndex::Max) currentMenuIndex_ = 0;
    }

    Vector4 normalColor = { 0.5f, 0.5f, 0.5f, 1.0f };
    Vector4 selectColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    if (startTextSprite_) {
        startTextSprite_->SetColor(currentMenuIndex_ == (int)MenuIndex::GameStart ? selectColor : normalColor);
    }
    if (settingTextSprite_) {
        settingTextSprite_->SetColor(currentMenuIndex_ == (int)MenuIndex::Setting ? selectColor : normalColor);
    }

    if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
        if (currentMenuIndex_ == (int)MenuIndex::GameStart) {
            titleMode_ = TitleMode::SaveSelect;
            currentSaveSlotIndex_ = std::clamp(currentSaveSlotIndex_, 0, GameDataManager::kSaveSlotCount - 1);
            DebugConsole::GetInstance()->AddLog("[Title] Open save slot select.");
        }
        else if (currentMenuIndex_ == (int)MenuIndex::Setting) {
            DebugConsole::GetInstance()->AddLog("[Title] Settings scene is not implemented yet.");
            // SceneManager::GetInstance()->ChangeScene("SETTING"); 
        }
    }
}

void TitleScene::UpdateSaveSelect() {
    InputManager* input = InputManager::GetInstance();

    const bool moveLeft =
        input->IsKeyTriggered(DIK_LEFT) ||
        input->IsKeyTriggered(DIK_UP) ||
        input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT) ||
        input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP);
    const bool moveRight =
        input->IsKeyTriggered(DIK_RIGHT) ||
        input->IsKeyTriggered(DIK_DOWN) ||
        input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT) ||
        input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN);

    if (moveLeft) {
        currentSaveSlotIndex_--;
        if (currentSaveSlotIndex_ < 0) {
            currentSaveSlotIndex_ = GameDataManager::kSaveSlotCount - 1;
        }
    }

    if (moveRight) {
        currentSaveSlotIndex_++;
        if (currentSaveSlotIndex_ >= GameDataManager::kSaveSlotCount) {
            currentSaveSlotIndex_ = 0;
        }
    }

    if (input->IsKeyTriggered(DIK_ESCAPE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
        titleMode_ = TitleMode::MainMenu;
        DebugConsole::GetInstance()->AddLog("[Title] Close save slot select.");
        return;
    }

    if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
        StartSelectedSaveSlot();
    }
}

void TitleScene::InitializeSaveSlotUI() {
    titleUiSprites_.clear();
    saveSlotCards_.fill(nullptr);
    saveSlotIcons_.fill(nullptr);
    for (auto& dots : saveSlotProgressDots_) {
        dots.fill(nullptr);
    }
    saveSelectHeader_ = nullptr;

    const float screenW = static_cast<float>(WinApp::kClientWidth);
    const float screenH = static_cast<float>(WinApp::kClientHeight);
    const float centerY = screenH * 0.62f;
    const float slotStep = 280.0f;
    const std::array<Vector2, 3> slotPositions = {
        Vector2{ screenW * 0.5f - slotStep, centerY },
        Vector2{ screenW * 0.5f, centerY },
        Vector2{ screenW * 0.5f + slotStep, centerY }
    };

    saveSelectHeader_ = CreateUISprite(
        "Resources/sprite/common/white.png",
        { screenW * 0.5f, screenH * 0.26f },
        { 520.0f, 8.0f },
        { 0.55f, 0.95f, 1.0f, 0.85f }
    );

    for (int i = 0; i < GameDataManager::kSaveSlotCount; ++i) {
        const Vector2& pos = slotPositions[i];
        saveSlotCards_[i] = CreateUISprite(
            "Resources/sprite/common/white.png",
            pos,
            { 230.0f, 230.0f },
            { 0.08f, 0.24f, 0.32f, 0.82f }
        );
        saveSlotIcons_[i] = CreateUISprite(
            "Resources/sprite/title/slime_save_icon.png",
            { pos.x, pos.y - 32.0f },
            { 122.0f, 122.0f },
            { 1.0f, 1.0f, 1.0f, 1.0f }
        );

        for (int dot = 0; dot < 3; ++dot) {
            saveSlotProgressDots_[i][dot] = CreateUISprite(
                "Resources/sprite/common/white.png",
                { pos.x - 36.0f + dot * 36.0f, pos.y + 76.0f },
                { 18.0f, 18.0f },
                { 0.35f, 0.42f, 0.48f, 0.8f }
            );
        }
    }
}

Sprite* TitleScene::CreateUISprite(const std::string& texturePath, const Vector2& position, const Vector2& size, const Vector4& color) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteCommon_.get(), texturePath);
    sprite->SetPosition(position);
    sprite->SetSize(size);
    sprite->SetColor(color);
    sprite->SetAnchorPoint({ 0.5f, 0.5f });
    sprite->SetVisible(false);
    sprite->Update();

    Sprite* raw = sprite.get();
    titleUiSprites_.push_back(std::move(sprite));
    return raw;
}

void TitleScene::UpdateSaveSlotUI() {
    const bool inSaveSelect = titleMode_ == TitleMode::SaveSelect;
    if (startTextSprite_) startTextSprite_->SetVisible(!inSaveSelect);
    if (settingTextSprite_) settingTextSprite_->SetVisible(!inSaveSelect);
    if (saveSelectHeader_) saveSelectHeader_->SetVisible(inSaveSelect);

    for (int i = 0; i < GameDataManager::kSaveSlotCount; ++i) {
        const bool selected = inSaveSelect && i == currentSaveSlotIndex_;
        const GameDataManager::SaveSlotSummary summary = GameDataManager::GetInstance()->GetSlotSummary(i);

        const float pulse = selected ? (0.5f + 0.5f * std::sin(titleUiTime_ * 5.0f)) : 0.0f;
        const Vector4 cardColor = selected
            ? Vector4{ 0.18f + pulse * 0.08f, 0.58f + pulse * 0.12f, 0.72f + pulse * 0.16f, 0.94f }
            : Vector4{ 0.08f, 0.22f, 0.30f, summary.exists ? 0.78f : 0.50f };
        const Vector4 iconColor = summary.exists
            ? Vector4{ 1.0f, 1.0f, 1.0f, selected ? 1.0f : 0.82f }
            : Vector4{ 0.62f, 0.76f, 0.82f, selected ? 0.80f : 0.44f };

        if (saveSlotCards_[i]) {
            saveSlotCards_[i]->SetVisible(inSaveSelect);
            saveSlotCards_[i]->SetColor(cardColor);
            saveSlotCards_[i]->SetSize(selected ? Vector2{ 252.0f, 252.0f } : Vector2{ 230.0f, 230.0f });
        }
        if (saveSlotIcons_[i]) {
            saveSlotIcons_[i]->SetVisible(inSaveSelect);
            saveSlotIcons_[i]->SetColor(iconColor);
            saveSlotIcons_[i]->SetSize(selected ? Vector2{ 138.0f, 138.0f } : Vector2{ 122.0f, 122.0f });
        }

        const int stageDots = std::clamp(summary.clearedStageCount, 0, 3);
        for (int dot = 0; dot < 3; ++dot) {
            Sprite* dotSprite = saveSlotProgressDots_[i][dot];
            if (!dotSprite) continue;

            Vector4 dotColor = { 0.26f, 0.32f, 0.38f, 0.78f };
            if (summary.tutorialCleared && dot == 0) {
                dotColor = { 0.35f, 0.95f, 1.0f, 0.92f };
            }
            if (dot < stageDots) {
                dotColor = { 1.0f, 0.86f, 0.22f, 0.96f };
            }
            if (!summary.exists) {
                dotColor = { 0.24f, 0.28f, 0.32f, 0.42f };
            }

            dotSprite->SetVisible(inSaveSelect);
            dotSprite->SetColor(dotColor);
            dotSprite->SetSize(selected ? Vector2{ 21.0f, 21.0f } : Vector2{ 18.0f, 18.0f });
        }
    }
}

void TitleScene::StartSelectedSaveSlot() {
    GameDataManager* saveData = GameDataManager::GetInstance();
    const GameDataManager::SaveSlotSummary summary = saveData->GetSlotSummary(currentSaveSlotIndex_);

    saveData->SetActiveSlot(currentSaveSlotIndex_);
    if (!summary.exists) {
        saveData->ResetAll();
    }

    const bool tutorialCleared = saveData->IsStageCleared(-1);
    DebugConsole::GetInstance()->AddLog(tutorialCleared ? "[Title] Start from stage select." : "[Title] Start tutorial.");
    SceneManager::GetInstance()->ChangeScene(tutorialCleared ? "SELECT" : "TUTORIAL");
}

void TitleScene::DrawSaveSlotUI() {
    for (auto& sprite : titleUiSprites_) {
        sprite->Draw();
    }
}

void TitleScene::Draw() {
    // --- 一人称視点判定 ---
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
#ifndef _DEBUG
    if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    // --- 1. 不透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7) continue; // フォグ(7)も不透明パスから除外
        obj->Draw(pointLightRes, spotLightRes);
    }

    // --- 2. 中間描画 (弾・デバッグ) ---
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    LightEditor::GetInstance()->Draw3D();

    // --- 3. 透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1) { // 透明のみ描画
            obj->Draw(pointLightRes, spotLightRes);
        }
    }
    particleSystem_->Draw();

    // =======================================================
    // 4. ローカルフォグ (霧の箱) の描画！
    // =======================================================
    bool hasFog = false;
    for (auto& obj : objects) {
        if (obj->GetMaterialType() == 7) hasFog = true;
    }

    if (hasFog) {
        dxCommon_->PreDrawLocalFog();
        for (auto& obj : objects) {
            if (obj->GetMaterialType() == 7) {
                obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
            }
        }
        dxCommon_->PostDrawLocalFog();
    }

    // =======================================================
    // 5. GPUパーティクルの描画！
    // =======================================================
    dxCommon_->UpdateGrabTexture();
    dxCommon_->PreDrawLocalFog();
    if (camera) {
        GPUParticleManager::GetInstance()->Draw(
            dxCommon_->GetCommandList(),
            camera->GetViewMatrix(),
            camera->GetProjectionMatrix(),
            gpuParticleTexHandle_,
            dxCommon_->GetDepthSrvHandle()
        );
    }
    dxCommon_->PostDrawLocalFog();
}

// ====================================================================
// UI描画専用の関数
// ====================================================================
void TitleScene::DrawUI() {
    // --- 4. 2D描画 (UIスプライト) ---
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (auto& sprite : sprites_) {
        sprite->Draw();
    }
    DrawSaveSlotUI();
}

// シャドウマップ描画の実装
void TitleScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

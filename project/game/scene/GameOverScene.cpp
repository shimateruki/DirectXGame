#define NOMINMAX
#include "GameOverScene.h"
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
#include "Fade.h"
#include "PostEffect.h"
#include "GameDataManager.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kTitleLetterInterval = 0.18f;
constexpr float kTitleLetterPopDuration = 0.24f;
constexpr float kMenuRevealDelay = 0.28f;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseOutCubic(float t) {
    t = Clamp01(t);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float EaseOutBack(float t) {
    t = Clamp01(t);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float shifted = t - 1.0f;
    return 1.0f + c3 * shifted * shifted * shifted + c1 * shifted * shifted;
}

bool IsFadePlayingForSceneIntro() {
    const Fade::Status status = Fade::GetInstance()->GetStatus();
    return status == Fade::Status::FadeIn ||
        status == Fade::Status::FadeOut ||
        status == Fade::Status::IrisIn ||
        status == Fade::Status::IrisOut;
}
}

void GameOverScene::Initialize() {
    PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
    if (postParams) {
        postParams->slimeFadeIntensity = 0.0f;
        postParams->irisFadeIntensity = 0.0f;
        postParams->blackout = 0.0f;
        postParams->dangerVignette = 0.0f;
        postParams->damageFlash = 0.0f;
    }

    // --- 1. エンジン基盤の取得 ---
    dxCommon_ = DirectXCommon::GetInstance();
    dxCommon_->SetRenderClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 2. リソースのロード ---
    ModelManager::GetInstance()->LoadModel("Characters/player");
    LOG("GameOverScene Initialized!");

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/GameOver.mp3");

    // --- 3. 共通クラス・マネージャの初期化 ---
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

    // GPUパーティクルの初期化
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

    // --- 5. レベルデータの読み込み (LevelLoaderへ委譲) ---
    levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/gameOverScene.json");
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/gameOverScene.json");
    BindLayoutSprites();

    LightManager::GetInstance()->LoadState("Resources/json/light/gameOverScene.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("gameOver_camera.json");

    dxCommon_->FlushCommandQueue(false);
}

void GameOverScene::Finalize() {
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();

    objectManager_.reset();
    backgroundSprite_ = nullptr;
    titleLetters_.fill(nullptr);
    titleLetterBasePositions_.fill(Vector2{ 0.0f, 0.0f });
    titleLetterBaseSizes_.fill(Vector2{ 0.0f, 0.0f });
    menuRows_ = {};
    sprites_.clear();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void GameOverScene::Update(float deltaTime) {
    const bool canAdvanceIntro = !IsFadePlayingForSceneIntro();
    const float introDeltaTime = canAdvanceIntro ? deltaTime : 0.0f;
    sceneTime_ += introDeltaTime;
    titleRevealTimer_ += introDeltaTime;
    if (IsTitleRevealComplete()) {
        UpdateMenuInput();
    }
    UpdateMenuSprites(introDeltaTime);

    // マネージャ・エディタ更新
    LightEditor::GetInstance()->Update();
    CameraEditor::GetInstance()->Update(player_, false);
    CameraManager::GetInstance()->Update();

    // オブジェクト一括更新 (ObjectManager)
    objectManager_->Update(deltaTime);
    particleSystem_->Update(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }

    // 各種マネージャ
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
}

void GameOverScene::Draw() {
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
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || IsSpecialMaterialType(obj->GetMaterialType())) continue; 
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
    DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);

    // =======================================================
    // 5. GPUパーティクルの描画！
    // =======================================================
    const bool grabUpdated = DrawSpecialMaterialObjects(objects, dxCommon_, BulletManager::GetInstance(), player_, isFirstPerson);
    DrawGPUParticles(dxCommon_, camera, gpuParticleTexHandle_, grabUpdated);
}

// ====================================================================
// UI描画専用の関数
// ====================================================================
void GameOverScene::DrawUI() {
    // --- 4. 2D描画 (UIスプライト) ---
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (auto& sprite : sprites_) {
        sprite->Draw();
    }
}

void GameOverScene::BindLayoutSprites() {
    backgroundSprite_ = FindSprite("gameover_background");

    constexpr std::array<const char*, 7> titleNames = {
        "gameover_letter_g",
        "gameover_letter_a",
        "gameover_letter_m",
        "gameover_letter_e1",
        "gameover_letter_o",
        "gameover_letter_v",
        "gameover_letter_e2"
    };

    for (size_t i = 0; i < titleNames.size(); ++i) {
        titleLetters_[i] = FindSprite(titleNames[i]);
        if (titleLetters_[i]) {
            titleLetterBasePositions_[i] = titleLetters_[i]->GetPosition();
            titleLetterBaseSizes_[i] = titleLetters_[i]->GetSize();
        }
    }

    constexpr std::array<const char*, 2> rowPrefixes = {
        "gameover_retry",
        "gameover_title"
    };

    for (size_t i = 0; i < rowPrefixes.size(); ++i) {
        MenuRow& row = menuRows_[i];
        const std::string prefix = rowPrefixes[i];
        row.backdrop = FindSprite(prefix + "_row");
        row.label = FindSprite(prefix + "_label");
        if (row.backdrop) {
            row.backdropBaseSize = row.backdrop->GetSize();
        }
        if (row.label) {
            row.labelBaseSize = row.label->GetSize();
        }
    }
}

Sprite* GameOverScene::FindSprite(const std::string& name) const {
    for (const auto& sprite : sprites_) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr;
}

void GameOverScene::UpdateMenuInput() {
    if (!inputManager_) {
        return;
    }

    const bool up =
        inputManager_->IsKeyTriggered(DIK_UP) ||
        inputManager_->IsKeyTriggered(DIK_W) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP);
    const bool down =
        inputManager_->IsKeyTriggered(DIK_DOWN) ||
        inputManager_->IsKeyTriggered(DIK_S) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN);

    if (up) {
        ChangeSelection(-1);
    }
    if (down) {
        ChangeSelection(1);
    }

    if (inputManager_->IsKeyTriggered(DIK_SPACE) ||
        inputManager_->IsKeyTriggered(DIK_RETURN) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
        ConfirmSelection();
    }
}

void GameOverScene::UpdateMenuSprites(float deltaTime) {
    (void)deltaTime;

    if (backgroundSprite_) {
        backgroundSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
    }

    const Vector4 hiddenTitleColor = { 1.0f, 0.16f, 0.10f, 0.0f };

    for (size_t i = 0; i < titleLetters_.size(); ++i) {
        Sprite* letter = titleLetters_[i];
        if (!letter) {
            continue;
        }

        const float appearTime = static_cast<float>(i) * kTitleLetterInterval;
        const float revealElapsed = titleRevealTimer_ - appearTime;
        const float wave = std::sin(sceneTime_ * 2.2f + static_cast<float>(i) * 0.42f);
        const float pulse = 0.5f + 0.5f * std::sin(sceneTime_ * 3.0f + static_cast<float>(i) * 0.28f);
        const Vector2 basePos = titleLetterBasePositions_[i];
        const Vector2 baseSize = titleLetterBaseSizes_[i];

        if (revealElapsed < 0.0f) {
            letter->SetVisible(false);
            letter->SetPosition(basePos);
            letter->SetSize({ baseSize.x * 0.65f, baseSize.y * 0.65f });
            letter->SetColor(hiddenTitleColor);
            continue;
        }

        letter->SetVisible(true);
        const float revealRate = Clamp01(revealElapsed / kTitleLetterPopDuration);
        const float revealEase = EaseOutCubic(revealRate);
        const float popEase = EaseOutBack(revealRate);
        const float idleScale = 1.0f + pulse * 0.025f;
        const float revealScale = std::clamp(0.55f + popEase * 0.48f, 0.55f, 1.12f);
        const float scale = revealRate < 1.0f ? revealScale : idleScale;
        const float dropOffset = (1.0f - revealEase) * -34.0f;

        letter->SetPosition({ basePos.x, basePos.y + dropOffset + wave * 4.0f * revealEase });
        letter->SetSize({ baseSize.x * scale, baseSize.y * scale });
        letter->SetColor({
            0.92f + pulse * 0.08f,
            0.10f + pulse * 0.10f,
            0.08f + pulse * 0.08f,
            revealEase
        });
    }

    const float pulse = 0.5f + 0.5f * std::sin(sceneTime_ * 5.4f);
    const Vector4 normalText = { 0.58f, 0.72f, 0.82f, 0.74f };
    const Vector4 selectedText = { 0.28f + pulse * 0.18f, 0.84f + pulse * 0.12f, 1.0f, 1.0f };
    const float menuStartTime = static_cast<float>(titleLetters_.size()) * kTitleLetterInterval + kMenuRevealDelay;
    const float menuAlpha = EaseOutCubic((titleRevealTimer_ - menuStartTime) / 0.28f);
    const bool menuVisible = menuAlpha > 0.0f;

    for (int i = 0; i < static_cast<int>(MenuItem::Count); ++i) {
        MenuRow& row = menuRows_[static_cast<size_t>(i)];
        const bool selected = selectedIndex_ == i;
        const float rowScale = selected ? 1.04f + pulse * 0.025f : 0.98f;
        const float labelScale = selected ? 1.08f + pulse * 0.06f : 0.96f;

        if (row.backdrop) {
            row.backdrop->SetVisible(menuVisible);
            const uint32_t handle = Sprite::LoadTexture(selected ? "ui/settings/settings_row_selected.png" : "ui/settings/settings_row.png");
            row.backdrop->SetTextureHandle(handle);
            const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
            row.backdrop->SetTextureRect({ 0.0f, 0.0f }, { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
            row.backdrop->SetSize({ row.backdropBaseSize.x * rowScale, row.backdropBaseSize.y * rowScale });
            Vector4 backdropColor = selected
                ? Vector4{ 0.38f + pulse * 0.12f, 0.90f, 1.0f, 0.98f }
                : Vector4{ 0.42f, 0.68f, 0.80f, 0.54f };
            backdropColor.w *= menuAlpha;
            row.backdrop->SetColor(backdropColor);
        }

        if (row.label) {
            row.label->SetVisible(menuVisible);
            row.label->SetSize({ row.labelBaseSize.x * labelScale, row.labelBaseSize.y * labelScale });
            Vector4 labelColor = selected ? selectedText : normalText;
            labelColor.w *= menuAlpha;
            row.label->SetColor(labelColor);
        }
    }
}

bool GameOverScene::IsTitleRevealComplete() const {
    const float menuStartTime = static_cast<float>(titleLetters_.size()) * kTitleLetterInterval + kMenuRevealDelay;
    return titleRevealTimer_ >= menuStartTime;
}

void GameOverScene::ChangeSelection(int direction) {
    const int count = static_cast<int>(MenuItem::Count);
    selectedIndex_ = (selectedIndex_ + direction + count) % count;
}

void GameOverScene::ConfirmSelection() {
    switch (static_cast<MenuItem>(selectedIndex_)) {
    case MenuItem::Retry:
        GameDataManager::GetInstance()->ResetLives();
        DebugConsole::GetInstance()->AddLog("[GameOver] Retry selected. Lives reset to 3.");
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        break;
    case MenuItem::Title:
        DebugConsole::GetInstance()->AddLog("[GameOver] Return title selected.");
        SceneManager::GetInstance()->ChangeScene("TITLE");
        break;
    default:
        break;
    }
}

// シャドウマップ描画の実装
void GameOverScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

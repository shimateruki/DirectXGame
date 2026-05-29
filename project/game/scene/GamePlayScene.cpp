#define NOMINMAX
#include "GamePlayScene.h"
#include "AudioPlayer.h"
#include "BossCore.h"
#include "BulletManager.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "GameProgress.h"
#include "GameRule.h"
#include "InputManager.h"
#include "LevelLoader.h"
#include "LightManager.h"
#include "LockOnSystem.h"
#include "ModelManager.h"
#include "MoveStrategy2D.h"
#include "MoveStrategy3D.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ObjectManager.h"
#include "ParticleSystem.h"
#include "SaveDataManager.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "TutorialDoll.h"
#include "WinApp.h"
#include "imgui.h"
#include "PlayerState.h"
#include <EventManager.h>
#include <cassert>

#ifdef _DEBUG
#include "ParticleEditor.h"
#endif

// --- JSON (保存機能) ---
#include "TimeAttackUI.h"
#include "json.hpp"
#include <BaseEnemy.h>
#include <CameraEditor.h>
#include <CinematicFade.h>
#include <EnemyFactory.h>
#include <EnemySpawner.h>
#include <GPUParticleManager.h>
#include <LightEditor.h>
#include <MeshEffectManager.h>
#include <ParticleManager.h>
#include <PostEffect.h>
#include <SrvManager.h>
#include <array>
#include <cmath>
#include <fstream>
#include <numbers>
#include <string>

namespace {
struct GameOverGlyphSource {
    const char* textureName;
    Vector2 sourceLeftTop;
    Vector2 sourceSize;
    float targetHeight;
};

constexpr std::array<GameOverGlyphSource, 7> kGameOverGlyphSources = {
    GameOverGlyphSource{ "UI/TextGe.png", { 41.0f, 36.0f }, { 57.0f, 54.0f }, 86.0f },
    GameOverGlyphSource{ "UI/Text-.png",  { 44.0f, 62.0f }, { 50.0f, 7.0f }, 14.0f },
    GameOverGlyphSource{ "UI/TextMu.png", { 43.0f, 41.0f }, { 53.0f, 48.0f }, 86.0f },
    GameOverGlyphSource{ "UI/TextO.png",  { 43.0f, 34.0f }, { 33.0f, 33.0f }, 86.0f },
    GameOverGlyphSource{ "UI/Text-.png",  { 44.0f, 62.0f }, { 50.0f, 7.0f }, 14.0f },
    GameOverGlyphSource{ "UI/TextBa.png", { 41.0f, 33.0f }, { 35.0f, 35.0f }, 86.0f },
    GameOverGlyphSource{ "UI/Text-.png",  { 44.0f, 62.0f }, { 50.0f, 7.0f }, 14.0f },
};

float Clamp01(float value) {
    return Math::Clamp(value, 0.0f, 1.0f);
}

float EaseOutCubic(float t) {
    t = Clamp01(t);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

Vector2 ResolveGameOverGlyphSize(const GameOverGlyphSource& source) {
    const float scale = source.targetHeight / source.sourceSize.y;
    return {
        source.sourceSize.x * scale,
        source.sourceSize.y * scale
    };
}
}

bool GamePlayScene::s_isRebooting_ = false;

GamePlayScene::GamePlayScene() {}
GamePlayScene::~GamePlayScene() {}

void GamePlayScene::SetSpriteTexturePreserveSize(Sprite* sprite, const std::string& textureName) {
    if (!sprite || sprite->GetTextureName() == textureName) {
        return;
    }

    Vector2 currentSize = sprite->GetSize();
    sprite->SetTextureHandle(Sprite::LoadTexture(textureName));
    sprite->SetTextureName(textureName);
    sprite->SetSize(currentSize);
}

void GamePlayScene::ApplyPauseInputUiIfNeeded() {
    const bool useGamepadUi = inputManager_ && inputManager_->IsGamepadMode();
    if (hasAppliedPauseInputUi_ && pauseUiUsesGamepad_ == useGamepadUi) {
        return;
    }

    SetSpriteTexturePreserveSize(
        tabPauseTextSprite_,
        useGamepadUi ? "tab_text_pad.png" : "tab_text.png");

    pauseUiUsesGamepad_ = useGamepadUi;
    hasAppliedPauseInputUi_ = true;

    DebugConsole::GetInstance()->AddLog(
        useGamepadUi
        ? "[PauseUI] Input display switched to Controller"
        : "[PauseUI] Input display switched to Keyboard");
}

void GamePlayScene::UpdatePauseMenuVisuals(float deltaTime) {
    pauseMenuBlinkTimer_ += deltaTime;

    const float blink = 0.5f + 0.5f * std::sin(pauseMenuBlinkTimer_ * 6.0f);
    const float selectedAlpha = 0.62f + 0.38f * blink;
    const float selectedScale = 1.07f + 0.05f * blink;

    auto ApplyEffect = [&](Sprite* sprite, bool isSelected, const Vector2& baseSize) {
        if (!sprite) {
            return;
        }

        sprite->SetColor(
            isSelected
            ? Vector4{ 1.0f, 1.0f, 1.0f, selectedAlpha }
            : Vector4{ 0.5f, 0.5f, 0.5f, 0.45f });
        sprite->SetSize(
            isSelected
            ? Vector2{ baseSize.x * selectedScale, baseSize.y * selectedScale }
            : baseSize);
        };

    ApplyEffect(restartPoseTextSprite_, currentPauseMenuIndex_ == (int)PauseMenuIndex::Restart, pauseRestartTextBaseSize_);
    ApplyEffect(optionPoseTextSprite_, currentPauseMenuIndex_ == (int)PauseMenuIndex::Option, pauseOptionTextBaseSize_);
    ApplyEffect(titleTextPoseSprite_, currentPauseMenuIndex_ == (int)PauseMenuIndex::Title, pauseTitleTextBaseSize_);
}

void GamePlayScene::InitializeGameOverTitleGlyphs() {
    gameOverTitleGlyphStrip_.glyphs.clear();
    gameOverTitleGlyphStrip_.baseOffsets.clear();
    gameOverTitleGlyphStrip_.baseSizes.clear();
    gameOverTitleGlyphStrip_.basePosition = gameOverTextSprite_
        ? gameOverTextSprite_->GetPosition()
        : Vector2{ 790.0f, 260.0f };
    gameOverTitleGlyphStrip_.animationTimer = 0.0f;
    gameOverTitleGlyphStrip_.initialized = false;

    if (!spriteCommon_) {
        return;
    }

    constexpr float spacing = 14.0f;
    float totalWidth = spacing * static_cast<float>(kGameOverGlyphSources.size() - 1);
    for (const auto& source : kGameOverGlyphSources) {
        totalWidth += ResolveGameOverGlyphSize(source).x;
    }

    float currentX = -totalWidth * 0.5f;
    for (const auto& source : kGameOverGlyphSources) {
        const Vector2 glyphSize = ResolveGameOverGlyphSize(source);

        auto glyph = std::make_unique<Sprite>();
        glyph->Initialize(spriteCommon_.get(), Sprite::LoadTexture(source.textureName));
        glyph->SetTextureName(source.textureName);
        glyph->SetTextureRect(source.sourceLeftTop, source.sourceSize);
        glyph->SetSize(glyphSize);
        glyph->SetPosition(gameOverTitleGlyphStrip_.basePosition);
        glyph->SetColor({ 1.0f, 0.12f, 0.14f, 0.0f });
        glyph->Update();

        gameOverTitleGlyphStrip_.baseOffsets.push_back({
            currentX + glyphSize.x * 0.5f,
            0.0f
        });
        gameOverTitleGlyphStrip_.baseSizes.push_back(glyphSize);
        gameOverTitleGlyphStrip_.glyphs.push_back(std::move(glyph));

        currentX += glyphSize.x + spacing;
    }

    gameOverTitleGlyphStrip_.initialized = true;

    gameOverEnterTextSprite_ = std::make_unique<Sprite>();
    gameOverEnterTextSprite_->Initialize(spriteCommon_.get(), Sprite::LoadTexture("enter_text.png"));
    gameOverEnterTextSprite_->SetTextureName("enter_text.png");
    gameOverEnterTextSprite_->SetPosition({ 1375.0f, 825.0f });
    gameOverEnterTextSprite_->SetSize(gameOverEnterTextBaseSize_);
    gameOverEnterTextSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    gameOverEnterTextSprite_->Update();
}

void GamePlayScene::ResetGameOverUiVisuals() {
    gameOverUiTimer_ = 0.0f;
    gameOverMenuBlinkTimer_ = 0.0f;
    currentGameOverMenuIndex_ = (int)GameOverMenuIndex::Restart;
    isGameOverUiReady_ = false;

    HideGameOverUi();
    gameOverTitleGlyphStrip_.animationTimer = 0.0f;
}

void GamePlayScene::UpdateGameOverTitleGlyphs(float deltaTime, float alpha) {
    if (!gameOverTitleGlyphStrip_.initialized) {
        return;
    }

    gameOverTitleGlyphStrip_.animationTimer += deltaTime;
    const float baseAlpha = Clamp01(alpha);

    for (int i = 0; i < static_cast<int>(gameOverTitleGlyphStrip_.glyphs.size()); ++i) {
        if (!gameOverTitleGlyphStrip_.glyphs[i] ||
            i >= static_cast<int>(gameOverTitleGlyphStrip_.baseOffsets.size()) ||
            i >= static_cast<int>(gameOverTitleGlyphStrip_.baseSizes.size())) {
            continue;
        }

        const float localTime = gameOverTitleGlyphStrip_.animationTimer - 0.075f * static_cast<float>(i);
        const float appearRate = Clamp01(localTime / 0.48f);
        const float fadeRate = Clamp01(localTime / 0.18f);
        const float eased = EaseOutCubic(appearRate);
        const float settle = std::sin(appearRate * 3.14159265f) * (1.0f - appearRate);
        const float direction = (i % 2 == 0) ? -1.0f : 1.0f;
        const Vector2& offset = gameOverTitleGlyphStrip_.baseOffsets[i];
        const Vector2& baseSize = gameOverTitleGlyphStrip_.baseSizes[i];
        const float entryOffsetY = -92.0f * (1.0f - eased) + 12.0f * settle;
        const float entryOffsetX = direction * 18.0f * (1.0f - eased);
        const float glyphScale = 0.78f + 0.22f * eased + 0.12f * settle;

        gameOverTitleGlyphStrip_.glyphs[i]->SetPosition({
            gameOverTitleGlyphStrip_.basePosition.x + offset.x + entryOffsetX,
            gameOverTitleGlyphStrip_.basePosition.y + offset.y + entryOffsetY
        });
        gameOverTitleGlyphStrip_.glyphs[i]->SetSize({
            baseSize.x * glyphScale,
            baseSize.y * glyphScale
        });
        gameOverTitleGlyphStrip_.glyphs[i]->SetRotation(direction * 0.12f * (1.0f - eased));
        gameOverTitleGlyphStrip_.glyphs[i]->SetColor({
            1.0f,
            0.12f,
            0.14f,
            baseAlpha * fadeRate
        });
        gameOverTitleGlyphStrip_.glyphs[i]->Update();
    }
}

void GamePlayScene::UpdateGameOverMenuVisuals(float deltaTime, float alpha) {
    const float menuAlpha = Clamp01(alpha);
    gameOverMenuBlinkTimer_ += deltaTime;
    const float blink = 0.5f + 0.5f * std::sin(gameOverMenuBlinkTimer_ * 6.0f);
    const float selectedAlpha = 0.62f + 0.38f * blink;
    const float selectedScale = 1.07f + 0.05f * blink;

    auto ApplyEffect = [&](Sprite* sprite, bool isSelected, const Vector2& baseSize) {
        if (!sprite) {
            return;
        }

        sprite->SetColor(
            isSelected
            ? Vector4{ 1.0f, 1.0f, 1.0f, selectedAlpha * menuAlpha }
            : Vector4{ 0.5f, 0.5f, 0.5f, 0.35f * menuAlpha });
        sprite->SetSize(
            isSelected
            ? Vector2{ baseSize.x * selectedScale, baseSize.y * selectedScale }
            : baseSize);
        };

    ApplyEffect(restartTextSprite_, currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Restart, gameOverRestartBaseSize_);
    ApplyEffect(titleTextSprite_, currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Title, gameOverTitleBaseSize_);

    if (gameOverEnterTextSprite_) {
        const bool useGamepadUi = inputManager_ && inputManager_->IsGamepadMode();
        if (gameOverUiUsesGamepad_ != useGamepadUi) {
            SetSpriteTexturePreserveSize(
                gameOverEnterTextSprite_.get(),
                useGamepadUi ? "enter_text_pad.png" : "enter_text.png");
            gameOverUiUsesGamepad_ = useGamepadUi;
        }

        const float inputAlpha = (0.45f + 0.55f * blink) * menuAlpha;
        const float inputScale = 1.0f + 0.035f * blink;
        gameOverEnterTextSprite_->SetColor({ 1.0f, 1.0f, 1.0f, inputAlpha });
        gameOverEnterTextSprite_->SetSize({
            gameOverEnterTextBaseSize_.x * inputScale,
            gameOverEnterTextBaseSize_.y * inputScale
        });
        gameOverEnterTextSprite_->Update();
    }
}

void GamePlayScene::DrawGameOverTitleGlyphs() {
    for (const auto& glyph : gameOverTitleGlyphStrip_.glyphs) {
        if (glyph) {
            glyph->Draw();
        }
    }
}

void GamePlayScene::HideGameOverUi() {
    auto SetAlphaZero = [](Sprite* sprite) {
        if (!sprite) {
            return;
        }

        Vector4 color = sprite->GetColor();
        color.w = 0.0f;
        sprite->SetColor(color);
    };

    SetAlphaZero(gameOverTextSprite_);
    SetAlphaZero(restartTextSprite_);
    SetAlphaZero(titleTextSprite_);

    for (auto& glyph : gameOverTitleGlyphStrip_.glyphs) {
        SetAlphaZero(glyph.get());
        if (glyph) {
            glyph->Update();
        }
    }

    if (gameOverEnterTextSprite_) {
        SetAlphaZero(gameOverEnterTextSprite_.get());
        gameOverEnterTextSprite_->SetSize(gameOverEnterTextBaseSize_);
        gameOverEnterTextSprite_->Update();
    }
}

void GamePlayScene::ApplyTutorialInputUiIfNeeded() {
    const bool useGamepadUi = inputManager_ && inputManager_->IsGamepadMode();
    if (hasAppliedTutorialInputUi_ && tutorialUiUsesGamepad_ == useGamepadUi) {
        return;
    }

    struct TutorialTextureSet {
        Sprite* sprite;
        const char* keyboardTexture;
        const char* gamepadTexture;
    };

    const TutorialTextureSet textureSets[] = {
        { tutorialMoveSprite_, "turrialTex/tutrialText_move.png", "turrialTex/tutrialText_move_pad.png" },
        { tutorialCameraSprite_, "turrialTex/tutrialText_cameraControl.png", "turrialTex/tutrialText_cameraControl_pad.png" },
        { tutorialJumpSprite_, "turrialTex/tutrialText_jump.png", "turrialTex/tutrialText_jump_pad.png" },
        { tutorialLockOnSprite_, "turrialTex/tutrialText_lockOn.png", "turrialTex/tutrialText_lockOn_pad.png" },
        { tutorialAttackSprite_, "turrialTex/tutrialText_attak.png", "turrialTex/tutrialText_attak_pad.png" },
        { tutorialFallAttackSprite_, "turrialTex/tutrialText_wallAttak.png", "turrialTex/tutrialText_wallAttak_pad.png" },
        { tutorialDodgeSprite_, "turrialTex/tutrialText_donge.png", "turrialTex/tutrialText_donge_pad.png" },
    };

    for (const auto& textureSet : textureSets) {
        SetSpriteTexturePreserveSize(
            textureSet.sprite,
            useGamepadUi ? textureSet.gamepadTexture : textureSet.keyboardTexture);
    }

    tutorialUiUsesGamepad_ = useGamepadUi;
    hasAppliedTutorialInputUi_ = true;

    DebugConsole::GetInstance()->AddLog(
        useGamepadUi
        ? "[TutorialUI] Input display switched to Controller"
        : "[TutorialUI] Input display switched to Keyboard");
}

void GamePlayScene::Initialize() {
    using json = nlohmann::json;

    // --- 1. エンジン基盤・リソース初期化 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    LOG("Game Initialized!");

    SaveDataManager::GetInstance()->Load();
    bgmTutorialHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/bgm/game/tutorial.mp3");
    bgmWindHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/bgm/game/Wind.mp3");
    bgmBattle01Handle_ = audioPlayer_->LoadSoundFile("Resources/audio/bgm/game/battle_01.mp3");
    bgmBattle02Handle_ = audioPlayer_->LoadSoundFile("Resources/audio/bgm/game/battle_02.mp3");
    bgmDefeatHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/bgm/defeat/defeat.mp3");

    seMissionHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/Mission.mp3");
    seMissionClear3Handle_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/MissionClear3.mp3");
    seFallBridgeHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Environment/FallBridge.mp3");
    seLockOnHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Player/LockOn.mp3");

    seCursorMove_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/SelectOpen1.mp3");
    seDecide_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/SelectOpen2.mp3");
    seCancel_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/SelectClose.mp3");


    // --- 演出用SEロード ---
    seElevatorHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Environment/Elevator.mp3");
    seOpenDoor1Handle_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Environment/OpenDoor1.mp3");
    seOpenDoor2Handle_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Environment/OpenDoor2.mp3");
    seBridgeMagmaHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Environment/magma.mp3");

    // --- 2. 各種マネージャ初期化 ---
    EventManager::GetInstance()->ClearAllListeners();
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(),
        "Resources/sprite/white.png");

    ParticleManager::GetInstance()->Initialize(particleSystem_.get());

    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets(
        "Resources/json/gpu_particles/");
    GPUParticleManager::GetInstance()->PrewarmPreset("playerattak");
    GPUParticleManager::GetInstance()->PrewarmPreset("player_dash");
    GPUParticleManager::GetInstance()->PrewarmPreset("boss_container_top");
    GPUParticleManager::GetInstance()->PrewarmPreset("boss_container_bottom");

    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    // --- 3. サブシステム初期化 ---
    objectManager_ = std::make_unique<ObjectManager>();

    lockOnSystem_ = std::make_unique<LockOnSystem>();
    lockOnSystem_->Initialize(inputManager_);
    uint32_t lockOnTex =
        TextureManager::GetInstance()->Load("Resources/sprite/lockOn.png");
    lockOnSprite_ = std::make_unique<Sprite>();
    lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
    lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f }); // 画像の中心を基準にする
    lockOnSprite_->SetSize({ 64.0f, 64.0f });      // アイコンのサイズ（適宜調整）
    BulletManager::GetInstance()->Initialize(object3dCommon_.get(),
        CollisionManager::GetInstance());

    MeshEffectManager::GetInstance()->Initialize(object3dCommon_.get());
    // パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
    gpuParticleTexHandle_ =
        TextureManager::GetInstance()->Load("Resources/sprite/white.png");

    // --- 5. レベルデータ読み込み (JSON) ---
    levelLoader_ = std::make_unique<LevelLoader>();
    levelLoader_->LoadObjectLayout(this,
        "Resources/json/3Dobject/bossStage.json");
    levelLoader_->LoadSpriteLayout(this,
        "Resources/json/sprite/sprite_layout.json");
    levelLoader_->LoadSpriteLayout(
        this, "Resources/json/sprite/option_ui.json"); // オプションUI用

    // option/poseBack.png スプライトを最背面（sprites_ の先頭）に移動する
    {
        auto it = std::find_if(sprites_.begin(), sprites_.end(), [](const auto& sprite) {
            return sprite && sprite->GetName() == "option/poseBack.png";
        });
        if (it != sprites_.end()) {
            auto poseBack = std::move(*it);
            sprites_.erase(it);
            sprites_.insert(sprites_.begin(), std::move(poseBack));
        }
    }
    LightManager::GetInstance()->LoadState(
        "Resources/json/light/light_layout.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("game_camera.json");

    timeAttackUI_ = std::make_unique<TimeAttackUI>();
    timeAttackUI_->Initialize(spriteCommon_.get());
    timeAttackUI_->SetPosition({ 1260.0f, 52.0f }, 0.75f);
    timeAttackUI_->SetScale(0.78f);

    // --- スプライトの中から探索
    for (auto& sprite : sprites_) {
        if (sprite->GetName() == "playerHpBar") {
            playerHpBarSprite_ = sprite.get();
            playerHpBarMaxWidth_ = sprite->GetSize().x;
        }
        else if (sprite->GetName() == "playerDamageBar") {
            playerDamageBarSprite_ = sprite.get();
        }
    }

    // =======================================================
    // プレイヤーの回避クールタイム用ゲージを動的生成
    // =======================================================
    if (playerHpBarSprite_) {
        uint32_t whiteTex = TextureManager::GetInstance()->Load("Resources/sprite/white.png");
        
        Vector2 hpPos = playerHpBarSprite_->GetPosition();
        // HPバーのアンカーが中央であることを考慮し、左端の座標を計算
        float hpLeftX = hpPos.x - (playerHpBarMaxWidth_ * 0.5f);
        Vector2 backSize = { playerHpBarMaxWidth_, 6.0f }; // ゲージの長さをHPバーと同じに
        
        // 背景 (暗いグレー)
        auto dashBack = std::make_unique<Sprite>();
        dashBack->Initialize(spriteCommon_.get(), whiteTex);
        dashBack->SetSize(backSize);
        dashBack->SetAnchorPoint({ 0.0f, 0.5f }); // 左端を基準にする
        dashBack->SetPosition({ hpLeftX + 140.0f, hpPos.y - 60.0f }); // ちょい左に戻す
        dashBack->SetColor({ 0.1f, 0.1f, 0.1f, 0.8f });
        dashBack->SetName("playerDashBackBar");
        
        // ゲージ本体 (水色)
        auto dashBar = std::make_unique<Sprite>();
        dashBar->Initialize(spriteCommon_.get(), whiteTex);
        dashBar->SetSize(backSize);
        dashBar->SetAnchorPoint({ 0.0f, 0.5f }); // 左端を基準にする
        dashBar->SetPosition({ hpLeftX + 140.0f, hpPos.y - 60.0f }); // 背景と同じ位置
        dashBar->SetColor({ 0.0f, 1.0f, 1.0f, 1.0f });
        dashBar->SetName("playerDashBar");
        
        playerDashBackSprite_ = dashBack.get();
        playerDashBarSprite_ = dashBar.get();
        playerDashBarMaxWidth_ = backSize.x;
        
        sprites_.push_back(std::move(dashBack));
        sprites_.push_back(std::move(dashBar));
    }

    // =======================================================
    // ゲームオーバー用UIの取得と初期化 (最初は透明にして隠す)
    // =======================================================
    gameOverTextSprite_ = GetSpriteByName("GameOverText.png");
    restartTextSprite_ = GetSpriteByName("restartText.png");
    titleTextSprite_ = GetSpriteByName("titleText.png");
    gameOverRestartBaseSize_ = restartTextSprite_ ? restartTextSprite_->GetSize() : Vector2{};
    gameOverTitleBaseSize_ = titleTextSprite_ ? titleTextSprite_->GetSize() : Vector2{};
    InitializeGameOverTitleGlyphs();

    auto SetAlphaZero = [](Sprite* sprite) {
        if (sprite) {
            Vector4 color = sprite->GetColor();
            color.w = 0.0f; // 透明度(Alpha)を0に
            sprite->SetColor(color);
        }
        };
    SetAlphaZero(gameOverTextSprite_);
    SetAlphaZero(restartTextSprite_);
    SetAlphaZero(titleTextSprite_);
    HideGameOverUi();
    isGameOverUiReady_ = false; // フラグのリセット
    for (auto& sprite : sprites_) {
        if (sprite->GetName() == "bossrHpBar") {
            bossHpBarSprite_ = sprite.get();
            bossHpBarMaxWidth_ = sprite->GetSize().x;
            SetAlphaZero(bossHpBarSprite_);
        }
        else if (sprite->GetName() == "bossDamageBar") {
            bossDamageBarSprite_ = sprite.get();
            SetAlphaZero(bossDamageBarSprite_);
        }
        else if (sprite->GetName() == "bariaHp.png") {
            barrierHpBarSprite_ = sprite.get();
            barrierHpBarMaxWidth_ = sprite->GetSize().x;
            SetAlphaZero(barrierHpBarSprite_);
        }
        else if (sprite->GetName() == "barrierDamageBar") {
            barrierDamageBarSprite_ = sprite.get();
            SetAlphaZero(barrierDamageBarSprite_);
        }
        else if (sprite->GetName() == "bossHpBarback") {
            bossHpBackSprite_ = sprite.get();
            SetAlphaZero(bossHpBackSprite_);
        }
        else if (sprite->GetName() == "bossText") {
            bossNameSprite_ = sprite.get();
            SetAlphaZero(bossNameSprite_);
        }
        else if (sprite->GetName() == "bossIcon.png") {
            bossIconSprite_ = sprite.get();
            bossIconBasePos_ = bossIconSprite_->GetPosition();
            SetAlphaZero(bossIconSprite_);
        }
        else if (sprite->GetName() == "shieldIcon.png") {
            shieldIconSprite_ = sprite.get();
            shieldIconBasePos_ = shieldIconSprite_->GetPosition();
            SetAlphaZero(shieldIconSprite_);
        }
        else if (sprite->GetName() == "bossHpFrame.png") {
            bossHpFrameSprite_ = sprite.get();
            SetAlphaZero(bossHpFrameSprite_);
        }
        else if (sprite->GetName() == "bariaFrame.png") {
            bariaFrameSprite_ = sprite.get();
            SetAlphaZero(bariaFrameSprite_);
        }
        else if (sprite->GetName() == "hpFrame.png") {
            hpFrameSprite_ = sprite.get();
            SetAlphaZero(hpFrameSprite_);
        }
    }

    // =======================================================
    // ポーズ用UIの取得と初期化 (最初は透明にして隠す)
    // =======================================================
    poseBackSprite_ = GetSpriteByName("poseBack.png");
    poseTextSprite_ = GetSpriteByName("poseText.png");
    restartPoseTextSprite_ = GetSpriteByName("restartPoseText.png");
    titleTextPoseSprite_ = GetSpriteByName("titleTextPose.png");
    optionPoseTextSprite_ = GetSpriteByName("optionText.png");
    tabPauseTextSprite_ = GetSpriteByName("tab_text.png");
    optionControlsSprite_ = GetSpriteByName("option_controls.png");

    if (poseBackSprite_) {
        poseBackSprite_->SetPosition({ 960.0f, 540.0f });
        poseBackSprite_->SetSize({ 1920.0f, 1080.0f });
    }
    pauseRestartTextBaseSize_ = restartPoseTextSprite_ ? restartPoseTextSprite_->GetSize() : Vector2{};
    pauseOptionTextBaseSize_ = optionPoseTextSprite_ ? optionPoseTextSprite_->GetSize() : Vector2{};
    pauseTitleTextBaseSize_ = titleTextPoseSprite_ ? titleTextPoseSprite_->GetSize() : Vector2{};

    auto SetAlpha = [](Sprite* sprite, float alpha) {
        if (sprite) {
            Vector4 color = sprite->GetColor();
            color.w = alpha;
            sprite->SetColor(color);
        }
        };

    SetAlpha(poseBackSprite_, 0.0f);
    SetAlpha(poseTextSprite_, 0.0f);
    SetAlpha(restartPoseTextSprite_, 0.0f);
    SetAlpha(titleTextPoseSprite_, 0.0f);
    SetAlpha(optionPoseTextSprite_, 0.0f);
    SetAlpha(optionControlsSprite_, 0.0f);
    isPaused_ = false;
    ApplyPauseInputUiIfNeeded();

    // 1. まず objectManager からオブジェクトのリストを取得する
    auto& objects = objectManager_->GetObjects();

    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if ((*it)->GetName() == "Enemy_BossCore") {
            // 1. 古いボスの「今の住所」をメモ（まだ消さない）
            Object3d* oldAddress = it->get();

            // 2. 新しい BossCore を準備（まだリストには入れない）
            auto newBoss = std::make_unique<BossCore>();
            newBoss->SetSceneManager(
                SceneManager::GetInstance()); // シーンマネージャの設定
            newBoss->Initialize(object3dCommon_.get(),
                oldAddress->GetModelName()); // 新しいモデルで初期化
            newBoss->CopyFrom(oldAddress);                   // 座標などをコピー
            newBoss->SetClassName("BossCore");               // jsonからのコピー後に確実にクラス名を設定
            newBoss->SetTarget(player_); // プレイヤーをターゲットに設定
            this->boss_ = newBoss.get(); // コントロール用ポインタを保存
            BossCore* newAddress = newBoss.get();

            // ここが重要：古いボスが消える「前」に全てを繋ぎ直す

            // (A) 当たり判定マネージャから古いボスを抹消し、新しいボスを登録する
            // ※ もし Remove/Add 関数がない場合は、後述の「強硬手段」を使ってください
            CollisionManager::GetInstance()->RemoveObject(oldAddress);
            CollisionManager::GetInstance()->AddObject(newAddress);

            // (B) 子供たちの親を、古い住所から新しい住所へ書き換える
            for (auto& obj : objects) {
                if (obj->GetParent() == oldAddress) {
                    obj->SetParent(newAddress);

                    // 新しいボスにパーツを登録する
                    newAddress->AddArmorBlock(obj.get());
                }
            }

            // 3. 最後に実体を差し替える。ここで oldAddress は安全に消滅する
            *it = std::move(newBoss);
            break;
        }
    }

    // ボスコンテナのパーティクル発生
    if (boss_) {
        Vector3 bossPos = boss_->GetWorldPosition();
		Vector3 offsetTop = { 0.0f, 10.0f, 0.0f }; // ボスの頭上に少しオフセット
		Vector3 offsetBottom = { 0.0f, -5.0f, 0.0f }; // ボスの足元に少しオフセット
        bossContainerTopParticleId_ =
            GPUParticleManager::GetInstance()->PlayAutoEmitter(
                "boss_container_top", bossPos + offsetTop);
        bossContainerBottomParticleId_ =
            GPUParticleManager::GetInstance()->PlayAutoEmitter(
                "boss_container_bottom", bossPos + offsetBottom);
    }

    auto SetAlphaIfExists = [](Sprite* sprite, float a) {
        if (sprite) {
            Vector4 c = sprite->GetColor();
            c.w = a;
            sprite->SetColor(c);
        }
        };

    // --- チュートリアル用スプライトの取得（最初は非表示） ---
    tutorialMoveSprite_ = GetSpriteByName("tutrialText_move.png");
    tutorialCameraSprite_ = GetSpriteByName("tutrialText_cameraControl.png");
    tutorialJumpSprite_ = GetSpriteByName("tutrialText_jump.png");
    tutorialLockOnSprite_ = GetSpriteByName("tutrialText_lockOn.png");
    tutorialAttackSprite_ = GetSpriteByName("tutrialText_attak.png");
    tutorialFallAttackSprite_ = GetSpriteByName("tutrialText_wallAttak.png");
    tutorialDodgeSprite_ = GetSpriteByName("tutrialText_donge.png");
    ApplyTutorialInputUiIfNeeded();

    SetAlphaIfExists(tutorialMoveSprite_, 0.0f);
    SetAlphaIfExists(tutorialCameraSprite_, 0.0f);
    SetAlphaIfExists(tutorialJumpSprite_, 0.0f);
    SetAlphaIfExists(tutorialLockOnSprite_, 0.0f);
    SetAlphaIfExists(tutorialAttackSprite_, 0.0f);
    SetAlphaIfExists(tutorialFallAttackSprite_, 0.0f);
    SetAlphaIfExists(tutorialDodgeSprite_, 0.0f);

    for (auto& sprite : sprites_) {
        // 既存のHPバー取得
        if (sprite->GetName() == "playerHpBar") {
            playerHpBarSprite_ = sprite.get();
            playerHpBarMaxWidth_ = sprite->GetSize().x;
        }
        // ミッション用スプライトを名前で一致させて変数に保存する
        else if (sprite->GetName() == "missionText_mission.png")
            missionText_mission_ = sprite.get();
        else if (sprite->GetName() == "missionText_line.png")
            missionText_line_ = sprite.get();
        else if (sprite->GetName() == "missionText_Mark.png")
            missionText_Mark_ = sprite.get();
        else if (sprite->GetName() == "missionText_lever.png")
            missionText_lever_ = sprite.get();
        else if (sprite->GetName() == "missionText_go.png")
            missionText_go_ = sprite.get();
        else if (sprite->GetName() == "missionText_boss.png")
            missionText_boss_ = sprite.get();
    }

    SetAlphaZero(missionText_mission_);
    SetAlphaZero(missionText_line_);
    SetAlphaZero(missionText_Mark_);
    SetAlphaZero(missionText_lever_);
    SetAlphaZero(missionText_go_);
    SetAlphaZero(missionText_boss_);

    missionInitialShown_ = false;
    missionGoShown_ = false;
    missionBossShown_ = false;
    hasTutorialMovieFinished_ = false;

    // ミッション演出用の初期値を保存
    if (missionText_Mark_) missionMarkBaseSize_ = missionText_Mark_->GetSize();
    Vector2 missionTaskBaseSize = { 400.0f, 96.0f };
    if (missionText_line_ && missionText_line_->GetSize().y > 0.0f) {
        missionTaskBaseSize = missionText_line_->GetSize();
    }

    if (missionText_lever_) {
        missionLeverBasePos_ = missionText_lever_->GetPosition();
        missionLeverBaseSize_ = missionText_lever_->GetSize();
        if (missionLeverBaseSize_.y <= 0.0f) {
            missionLeverBaseSize_ = missionTaskBaseSize;
            missionText_lever_->SetSize(missionLeverBaseSize_);
        }
    }
    if (missionText_go_) {
        missionGoBasePos_ = missionText_go_->GetPosition();
        missionGoBaseSize_ = missionText_go_->GetSize();
        if (missionGoBaseSize_.y <= 0.0f) {
            missionGoBaseSize_ = missionTaskBaseSize;
            missionText_go_->SetSize(missionGoBaseSize_);
        }
    }
    if (missionText_boss_) {
        missionBossBasePos_ = missionText_boss_->GetPosition();
        missionBossBaseSize_ = missionText_boss_->GetSize();
        if (missionBossBaseSize_.y <= 0.0f) {
            missionBossBaseSize_ = missionTaskBaseSize;
            missionText_boss_->SetSize(missionBossBaseSize_);
        }
    }

    // =======================================================
    // チュートリアル矢印モデルの取得と初期化
    // =======================================================
    for (auto& obj : objectManager_->GetObjects()) {
        if (obj->GetName() == "arrow") {
            tutorialArrow_ = obj.get();
            tutorialArrowDefaultPos_ = obj->GetTransform()->translate;
            tutorialArrowWaypointIndex_ = 0;
            break;
        }
    }

    // =======================================================
    // 進行状況の復元：橋がすでに落ちている場合の処理
    // =======================================================
    if (GameProgress::GetInstance()->hasBridgeDropped) {
        // 1. シーン内の全ての「橋のブロック」を検索して消去・無効化
        auto& objects_ref = objectManager_->GetObjects();
        for (auto& obj : objects_ref) {
            std::string name = obj->GetName();
            // 名前が "Bridge_"" で始まるオブジェクトを全て対象にする
            if (name.find("Bridge_") != std::string::npos) {
                obj->SetCollisionAttribute(0); // 当たり判定を完全に消す
                if (name.find("Bridge_Block") !=
                    std::string::npos) {    // ブリッジブロックは完全に消す
                    obj->SetIsVisible(false); // 見えなくする
                    obj->isDead = true;       // 完全に消す（UpdateやDrawの対象から外す）
                }
            }
            else if (name.find("Battle_Field_Collision_Box_") !=
                std::string::npos) {
                obj->SetCollisionAttribute(kGround);
            }
            else if (name.find("Tutorial_") != std::string::npos) {
                if (name.find("Tutorial_Platform_02") != std::string::npos) {
                    continue;
                }
                else if (name.find("Tutorial_Platform_01") != std::string::npos) { // プラットフォーム装飾を確実に消す
                    auto& children = obj->GetChildren();
                    for (auto& child : children) {
                        child->SetCollisionAttribute(0); // 当たり判定を消す
                        child->SetIsVisible(false); // 見えなくする
                        child->isDead = true;       // 完全に消す（UpdateやDrawの対象から外す）
                    }
                }
				obj->SetCollisionAttribute(0); // 当たり判定を消す
                obj->SetIsVisible(false); // 見えなくする
                obj->isDead = true;       // 完全に消す（UpdateやDrawの対象から外す）
			}
            else if (name.find("arrow") != std::string::npos) {
                obj->SetIsVisible(false); // 見えなくする
            }
        }

        // 2. 演出フラグを立てて、ムービーが二度と再生されないようにする
        this->hasBridgeDropped_ = true;
        this->missionInitialShown_ = true; // チュートリアルミッションは表示済み
        this->missionGoShown_ = true;      // 次の「GO」を表示する状態にする
        // 3. プレイヤーの開始位置をボス前に飛ばし、チュートリアルをスキップ
        if (player_) {
            // 隊長が設定したボス前の座標を適用
            player_->GetTransform()->translate = { 0.0f, 1.3f, -68.0f };
            player_->UpdateLocalMatrix();
            player_->UpdateWorldMatrix();

            // チュートリアル完了扱いにする（進行度クラスとシーン内フラグの両方を更新）
            GameProgress::GetInstance()->hasFinishedTutorial = true;
            this->hasFinishedTutorial_ = true;
            this->hasTutorialMovieFinished_ = true; // スキップ時は完了扱い
            this->doorOpenProgress_ =
                1.0f; // チュートリアル部屋のドアも全開にしておく
        }
    }
    else {
        // 最初からプレイする場合の完全リセット
        // エディタ等でJSONが書き換わっていた場合でも確実に復活させる
        auto& objects_ref = objectManager_->GetObjects();
        for (auto& obj : objects_ref) {
            std::string name = obj->GetName();
            if (name.find("Tutorial_") != std::string::npos &&
                name.find("Ceiling") == std::string::npos &&
                name.find("Doll") == std::string::npos) {

                obj->SetIsVisible(true);
                obj->SetCollisionAttribute(kGround);

                // ドアは最初は閉まっている状態にする
                if (name == "Tutorial_Door_") {
                    obj->GetTransform()->translate.x = 0.0f; // 閉まった状態の位置
                }
                obj->UpdateWorldMatrix();
            }
            if (name.find("Tutorial_Doll_Button_") != std::string::npos) {
                obj->SetIsVisible(true);
				obj->SetCollisionAttribute(kGround);
                if (name.find("Tutorial_Doll_Button_01") != std::string::npos) {
                    obj->SetCollisionAttribute(kGround | kEnemy);
                }
                if (name.find("Tutorial_Doll_Button_03") != std::string::npos) {
                    Transform* trans = obj->GetTransform();
                    trans->translate.y = 0.0f;
                }
                if (name.find("Tutorial_Doll_Button_Collision_Box") != std::string::npos) {
                    Transform* trans = obj->GetTransform();
                    trans->translate.y = 23.63f;
                }
            }
            if (name.find("Bridge_") != std::string::npos) {
                if (name.find("Bridge_Collision") == std::string::npos) {
                    obj->SetIsVisible(true);
                }
                else if (name.find("Bridge_") !=
                    std::string::npos) { // ブリッジ関連は全て復活させる
                    if (name.find("Bridge_Collision") == std::string::npos) {
                        obj->SetIsVisible(true);
                    }
                    obj->SetCollisionAttribute(kGround);
                }
            }
            if (name.find("Battle_Field_Collision_") != std::string::npos) {
                obj->SetCollisionAttribute(kGround);
                if (name.find("Battle_Field_Collision_Box_South") !=
                    std::string::
                    npos) { // 南の当たり判定は最初は消しておく（橋が落ちるまでは通れるように）
                    obj->SetCollisionAttribute(0);
                }
            }
            if (name.find("arrow") != std::string::npos) {
                obj->SetIsVisible(true); // 見えなくする
            }
        }
        this->hasBridgeDropped_ = false;
        this->hasFinishedTutorial_ = false;
        this->doorOpenProgress_ = 0.0f; // ドアを閉める

        // =======================================================
        // チュートリアルプラットフォーム降下演出の初期化
        // =======================================================
        for (auto& obj : objects_ref) {
            if (obj->GetName() == "Tutorial_Platform_01") {
                this->tutorialPlatform_ = obj.get();
                // 初期位置を y:100 に (念のため)
                obj->GetTransform()->translate.y = 100.0f;
                obj->UpdateWorldMatrix();
                break;
            }
        }

        if (this->tutorialPlatform_ && player_) {
            // プレイヤーをプラットフォームの真上に配置
            // 本来の重力時の位置関係を維持するため、現状の差分をオフセットとして記録
            Vector3 platformPos = this->tutorialPlatform_->GetTransform()->translate;

            // プレイヤーを初期位置へ (x, z はプラットフォームに合わせ、y
            // は適切な高さへ) ユーザーの 94.7f という数値は、プラットフォーム 100.0f
            // に対して -5.3f のオフセットを示唆
            this->tutorialPlatformOffset_ = -5.3f;
            player_->GetTransform()->translate = {
                0.0f, platformPos.y + tutorialPlatformOffset_, -244.0f };
            player_->UpdateLocalMatrix();
            player_->UpdateWorldMatrix();

            // ムービー開始
            movieState_ = MovieState::kTutorialPlatformDescent;
            movieTimer_ = 0.0f;

            // エレベーター降下SEの再生（ループ）
            audioPlayer_->PlaySE(seElevatorHandle_, true, SaveDataManager::GetInstance()->GetSEVolume());
            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                CameraEditor::GetInstance()->PlayOverrideCamera(camera, "elevator_movie");
            }

            // 重力に任せると跳ねるため、物理を無効化して手動更新にする
            player_->SetIsControlActive(false);
            player_->SetIsPhysicsActive(false);
        }
    }

    audioPlayer_->StopBGM();
    bgmHandle_ = bgmTutorialHandle_;
    audioPlayer_->PlayBGM(bgmHandle_, true, SaveDataManager::GetInstance()->GetBGMVolume());

    // OptionUIの初期化
    optionUI_.Initialize(this, spriteCommon_.get());

    // =======================================================
    // リスタート演出（電脳リブート）と完全初期化
    // =======================================================
    SceneManager* scm = SceneManager::GetInstance();
    PostEffect::GetInstance()->ResetToBaseParams();

    if (scm->ShouldSkipFade()) {
        CinematicFade::GetInstance()->StartOpen(0.3f);
        scm->ResetSkipFade();
    }
    else {
        CinematicFade::GetInstance()->StartOpen(0.5f);
    }
    // --- 6. カメラの初期状態を強制的に反映（1フレーム目の Glide 防止） ---
    if (player_) {
        CameraEditor::GetInstance()->Update(player_, false);
        CameraManager::GetInstance()->Update();
    }

    // --- 7. 初期HPの同期 (演出用変数の初期化) ---
    if (player_) {
        playerVisualHp_ = Math::Clamp(player_->GetHp() / player_->GetMaxHp(), 0.0f, 1.0f);
        playerPrevHpRatio_ = playerVisualHp_;
    }
    if (boss_) {
        bossVisualHp_ = Math::Clamp(boss_->GetHp() / boss_->GetMaxHp(), 0.0f, 1.0f);
        bossPrevHpRatio_ = bossVisualHp_;

        float bRatio = Math::Clamp(boss_->GetBarrierHp() / boss_->GetMaxBarrierHp(), 0.0f, 1.0f);
        barrierVisualMain_ = bRatio;
        barrierVisualDamage_ = bRatio;
        barrierPrevHpRatio_ = bRatio;
    }

    dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {
    MeshEffectManager::GetInstance()->Clear();
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();
    particleSystem_.reset();
    particleCommon_.reset();
    sprites_.clear();
    spriteCommon_.reset();
    object3dCommon_.reset();
    objectManager_.reset();
    lockOnSystem_.reset();

    // パーティクルの停止
    GPUParticleManager::GetInstance()->StopAutoEmitter(bossContainerTopParticleId_);
    GPUParticleManager::GetInstance()->StopAutoEmitter(bossContainerBottomParticleId_);

    // ループ音の強制停止
    audioPlayer_->StopSe(seElevatorHandle_);
    audioPlayer_->StopSe(seOpenDoor2Handle_);
    StopBridgeMagmaSe();
}

void GamePlayScene::Update(float deltaTime) {
    float originalDeltaTime = deltaTime;
    ApplyPauseInputUiIfNeeded();
    ApplyTutorialInputUiIfNeeded();

    // プレイヤーが死亡して演出時間が経過したら、世界の時間を止める（ただし遷移中は止めない）
    if (player_ && player_->GetHp() <= 0.0f && player_->GetDeathTimer() > 3.5f && !isRestartTransition_ && !isTitleTransition_) {
        deltaTime = 0.0f;
    }

    if (HandleEscapeKey()) {
        return;
    }

    bool isGameOver = (player_ && player_->GetHp() <= 0.0f);
    bool isCinematicMode = IsCinematicMode();

    if (lockOnSystem_) {
        lockOnSystem_->SetEnabled(!isCinematicMode);
    }

    if (UpdatePauseAndOptionMenus(deltaTime, originalDeltaTime, isGameOver, isCinematicMode)) {
        return;
    }

    if (UpdateSceneTransition(originalDeltaTime)) {
        return;
    }

    UpdateTutorialDoor(deltaTime);

    static Math math;
    LightEditor::GetInstance()->Update();

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    UpdateMovieState(deltaTime);
    UpdateTutorialGuide(deltaTime);
    UpdateLockOnAndCamera(deltaTime, isCinematicMode, camera, math);
    UpdateSceneObjects(deltaTime);
    UpdateGameOver(originalDeltaTime);
    UpdateGameplaySystems(deltaTime);
    UpdateBossMovie(deltaTime);
    
    // ボスHP半減イベントの監視とBGM切り替え
    if (boss_ && hasBossAppeared_) {
        float hpRatio = boss_->GetHp() / boss_->GetMaxHp();
        if (hpRatio <= 0.5f && bgmHandle_ != bgmBattle02Handle_ && bgmHandle_ != bgmDefeatHandle_) {
            if (boss_->IsHpHalfEventActive()) {
                // 演出中は一度BGMを停止
                if (audioPlayer_->IsPlaying(bgmHandle_)) {
                    audioPlayer_->StopBGM();
                }
            }
            else {
                // 演出終了後にBGMをbattle_02.mp3に切り替えて再生
                bgmHandle_ = bgmBattle02Handle_;
                audioPlayer_->PlayBGM(bgmHandle_, true, SaveDataManager::GetInstance()->GetBGMVolume());
            }
        }
    }

    UpdateClearSequence(deltaTime);
}

bool GamePlayScene::HandleEscapeKey() {
#ifdef USE_IMGUI
    // ---------------------------------------------------------
    // 0. ESCキーでの強制終了（オプション画面以外）
    // ---------------------------------------------------------
    if (!isOptionMenu_ && inputManager_->IsKeyTriggered(DIK_ESCAPE)) {
        if (isPaused_) {
            // ポーズ中ならポーズを閉じる
            isPaused_ = false;
            auto SetAlpha = [](Sprite* sprite, float a) {
                if (sprite) {
                    Vector4 c = sprite->GetColor();
                    c.w = a;
                    sprite->SetColor(c);
                }
                };
            SetAlpha(poseBackSprite_, 0.0f);
            SetAlpha(poseTextSprite_, 0.0f);
            SetAlpha(restartPoseTextSprite_, 0.0f);
            SetAlpha(titleTextPoseSprite_, 0.0f);
            SetAlpha(optionPoseTextSprite_, 0.0f);
            SetAlpha(optionControlsSprite_, 0.0f);
            currentPauseMenuIndex_ = (int)PauseMenuIndex::Restart;
            pauseMenuBlinkTimer_ = 0.0f;
        }
        else {
            // ゲームプレイ中なら今まで通り終了
            PostQuitMessage(0);
        }
        return true;
    }
#endif
    return false;
}

bool GamePlayScene::UpdatePauseAndOptionMenus(float deltaTime, float originalDeltaTime, bool isGameOver, bool isCinematicMode) {
    // 【Pキー】 か パッドの【STARTボタン】でポーズ切り替え (ムービー中は不可)
    if (!isGameOver && !isCinematicMode &&
        inputManager_->IsActionTriggered("pose")) {
        if (isOptionMenu_) {
            // オプション表示中は、OptionUI::Update内部でTabキーを処理し、
            // 段階的に戻る挙動を行うため、ここでは処理しない
        }
        else {
            isPaused_ = !isPaused_; // フラグを反転
            if (isPaused_) {
                audioPlayer_->PlaySE(seDecide_, false, 1.0f);
            } else {
                audioPlayer_->PlaySE(seCancel_, false, 1.0f);
            }

            // 文字用のアルファ値 (1.0 = 完全不透明, 0.0 = 完全透明)
            float textAlpha = isPaused_ ? 1.0f : 0.0f;

            // 背景用のアルファ値 (0.6 = 半透明。もっと薄くしたければ 0.4 や 0.5 に)
            float backAlpha = isPaused_ ? 0.8f : 0.0f;
            auto SetAlpha = [](Sprite* sprite, float a) {
                if (sprite) {
                    Vector4 c = sprite->GetColor();
                    c.w = a;
                    sprite->SetColor(c);
                }
                };
            // 背景だけ backAlpha を使うように変更
            SetAlpha(poseBackSprite_, backAlpha);
            SetAlpha(poseTextSprite_, textAlpha);
            SetAlpha(restartPoseTextSprite_, textAlpha);
            SetAlpha(titleTextPoseSprite_, textAlpha);
            SetAlpha(optionPoseTextSprite_, textAlpha);
            SetAlpha(optionControlsSprite_, textAlpha);

            // 選択位置をリセット
            currentPauseMenuIndex_ = (int)PauseMenuIndex::Restart;
            pauseMenuBlinkTimer_ = 0.0f;
        }
    }

    // ---------------------------------------------------------
    // 2. ポーズ中のUI操作と遷移
    // ---------------------------------------------------------
    if (isOptionMenu_) {
        if (optionUI_.Update(deltaTime)) {
            isOptionMenu_ = false; // バック等で戻る
        }
    }
    else if (isPaused_) {
        bool cursorMoved = false;
        // 上下キーで項目切り替え
        if (inputManager_->IsActionTriggered("Forward")) {
            currentPauseMenuIndex_--;
            if (currentPauseMenuIndex_ < 0)
                currentPauseMenuIndex_ = (int)PauseMenuIndex::Max - 1;
            cursorMoved = true;
        }
        if (inputManager_->IsActionTriggered("Backward")) {
            currentPauseMenuIndex_++;
            if (currentPauseMenuIndex_ >= (int)PauseMenuIndex::Max)
                currentPauseMenuIndex_ = 0;
            cursorMoved = true;
        }
        if (cursorMoved) {
            audioPlayer_->PlaySE(seCursorMove_, false, 1.0f);
        }

        UpdatePauseMenuVisuals(originalDeltaTime);

        // 決定ボタンで遷移
        if (inputManager_->IsKeyTriggered(DIK_SPACE) || inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
            audioPlayer_->PlaySE(seDecide_, false, 1.0f);
            PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
            postParams->dangerVignette = 0.0f;
            postParams->blackout = 0.0f;
            if (currentPauseMenuIndex_ == (int)PauseMenuIndex::Restart) {
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }
            else if (currentPauseMenuIndex_ == (int)PauseMenuIndex::Option) {
                optionUI_.Reset();
                isOptionMenu_ = true; // 設定画面遷移
            }
            else if (currentPauseMenuIndex_ == (int)PauseMenuIndex::Title) {
                SceneManager::GetInstance()->ChangeScene("TITLE");
            }
        }
    }
    else {
        // ポーズメニュー以外はoptionText.pngを非表示
        if (optionPoseTextSprite_) {
            Vector4 color = optionPoseTextSprite_->GetColor();
            color.w = 0.0f;
            optionPoseTextSprite_->SetColor(color);
        }
    }

    // UI表示切り替え（オプション中は他のUIを隠す）
    for (auto& sprite : sprites_) {
        bool isOpt = optionUI_.IsOptionSprite(sprite.get());
        if (isOptionMenu_) {
            if (isOpt) {
                sprite->SetVisible(optionUI_.IsSpriteVisibleInCurrentTab(sprite.get()));
            }
            else {
                sprite->SetVisible(false);
            }
        }
        else {
            if (isOpt) {
                sprite->SetVisible(false);
            }
            else {
                sprite->SetVisible(true);
            }
        }
    }

    if (isPaused_ || isOptionMenu_) {
        for (auto& sprite : sprites_) {
            sprite->Update();
        }
        UpdateUI(originalDeltaTime); // ポーズ中もUIアニメーションは動かす
        return true;
    }
    return false;
}

bool GamePlayScene::UpdateSceneTransition(float originalDeltaTime) {
    // =======================================================
    // ゲームオーバー・リトライ遷移処理 (復旧)
    // =======================================================
    if (isRestartTransition_ || isTitleTransition_) {
        restartTimer_ += originalDeltaTime;
        float transitionDuration = 1.0f;
        float t = Math::Clamp(restartTimer_ / transitionDuration, 0.0f, 1.0f);

        PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
        if (postParams) {
            // CRTシャットダウン演出（縦に潰れる）
            postParams->crtShutdown = t;
        }

        // 完全に終了（1秒経過）したらシーンを切り替え
        if (restartTimer_ >= transitionDuration) {
            if (isRestartTransition_) {
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }
            else {
                SceneManager::GetInstance()->ChangeScene("TITLE");
            }
        }
        return true; // 遷移中はこれ以降の更新をスキップ
    }
    return false;
}

void GamePlayScene::UpdateTutorialDoor(float deltaTime) {
    // =======================================================
    // チュートリアルドアの処理
    // =======================================================
    if (!hasFinishedTutorial_) {
        for (auto& obj : objectManager_->GetObjects()) {
            if (obj->GetName() == "Tutorial_Doll_Button_01") {
                TutorialDoll* doll = dynamic_cast<TutorialDoll*>(obj.get());
                if (doll && doll->HasBeenDefeatedAtLeastOnce()) {
                    hasFinishedTutorial_ = true;
                    audioPlayer_->PlaySE(seMissionClear3Handle_, false, SaveDataManager::GetInstance()->GetSEVolume());

                    // ムービー開始
                    movieState_ = MovieState::kTutorialDoorOpen;
                    movieTimer_ = 0.0f;
                    audioPlayer_->StopBGM();
                    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                    CameraEditor::GetInstance()->PlayOverrideCamera(camera, "tutorial movie");

                    // ドア開閉SEの再生 (開始音と、開放中ループ音)
                    audioPlayer_->PlaySE(seOpenDoor1Handle_, false, SaveDataManager::GetInstance()->GetSEVolume());
                    audioPlayer_->PlaySE(seOpenDoor2Handle_, true, SaveDataManager::GetInstance()->GetSEVolume());

                    break;
                }
            }
        }
    }

    if (hasFinishedTutorial_) {
        if (doorOpenProgress_ < 1.0f) {
            doorOpenProgress_ += deltaTime * 0.5f; // 2秒で開く
            if (doorOpenProgress_ > 1.0f) {
                doorOpenProgress_ = 1.0f;
                // ドア開放中SEの停止
                audioPlayer_->StopSe(seOpenDoor2Handle_);

                // ドアが完全に開いた瞬間モデルを消しておく
                for (auto& obj : objectManager_->GetObjects()) {
                    std::string name = obj->GetName();
					if (name.find("Tutorial_Door") != std::string::npos) {
                        obj->SetCollisionAttribute(0); // 当たり判定も消す
                    }
                    if (name.find("Tutorial_Doll_Button_01") != std::string::npos) {
                        obj->SetCollisionAttribute(0); // ボタンの被攻撃用当たり判定も消す
                    }
                    if (name.find("Tutorial_Doll_Button_02") != std::string::npos) {
						obj->SetIsVisible(false); // ボタンカバーも消す
                    }
                    if (name.find("Tutorial_Doll_Button_03") != std::string::npos) {
						// ボタンを押下状態にする
                        Transform* trans = obj->GetTransform();
						trans->translate.y = -0.5f; // 少し沈み込む
                    }
                    if (name.find("Tutorial_Doll_Button_Collision_Box") != std::string::npos) {
                        // ボタンの壁判定を下げる
                        Transform* trans = obj->GetTransform();
                        trans->translate.y = 21.13f; // 少し沈み込む
                    }
                }

                // ここで missionText_go を表示（1回だけ）
                if (!missionGoShown_ && missionText_go_) {
                    missionGoShown_ = true;
                }
            }
        }
        for (auto& obj : objectManager_->GetObjects()) {
            if (obj->GetName() == "Tutorial_Door_Left") {
                Transform* trans = obj->GetTransform();
				trans->translate.x = 9.8f * doorOpenProgress_; // 親のbaseの都合上、左ドアは正方向に動かす
                trans->isQuaternionMaster = false;
                obj->UpdateWorldMatrix();
            }
            else if (obj->GetName() == "Tutorial_Door_Right") {
                Transform* trans = obj->GetTransform();
				trans->translate.x = -9.8f * doorOpenProgress_; // 右ドアは負方向に動かす
                trans->isQuaternionMaster = false;
                obj->UpdateWorldMatrix();
            }
        }
    }
}

void GamePlayScene::UpdateMovieState(float deltaTime) {
    // =================================================================
    // ムービーの制御
    // =================================================================
    if (movieState_ == MovieState::kBridgeDrop) {
        // ムービー開始時の初期化
        if (movieTimer_ == 0.0f) {
            movieStoredPlayerPos_ = player_->GetWorldPosition();
            player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
            player_->SetIsControlActive(false);
            player_->SetIsPhysicsActive(false);
        }

        movieTimer_ += deltaTime;

        // プレイヤーの座標を強制固定
        // (Player::Update側でも物理が無効化されているため)
        player_->SetTranslate(movieStoredPlayerPos_);

        // ムービー開始から1.5秒後にブリッジブロックの崩落演出を開始する

        // カメラ制御は GhostRecorder に任せるため、ブロックの崩落演出のみ実行する
        if (movieTimer_ > 1.5f) {
            float magmaSurfaceY = -49.5f;
            for (const auto& obj : objectManager_->GetObjects()) {
                if (obj && obj->GetName() == "maguma") {
                    magmaSurfaceY = obj->GetWorldPosition().y;
                    break;
                }
            }
            const float bridgeMagmaEnterY = magmaSurfaceY + 1.0f;

            // まず親の当たり判定を無効化する（プレイヤーが落ちるように）
            for (auto& obj : objectManager_->GetObjects()) {
                if (obj->GetName() == "Bridge_Block_Front") {
                    obj->SetCollisionAttribute(0);
                }
            }

            for (auto& obj : objectManager_->GetObjects()) {
                if (obj->GetName() == "Bridge_Block_Center") {
                    Transform* trans = obj->GetTransform();
                    const bool isSinking = bridgeCenterMagmaImpactPlayed_;
                    trans->translate.y -= (isSinking ? 4.0f : 26.0f) * deltaTime;
                    trans->rotate.x -= (isSinking ? 0.35f : 1.0f) * deltaTime; // 自然な傾き（下へ折れ曲がる）
                    trans->isQuaternionMaster = false;
                    obj->UpdateWorldMatrix();
                    if (!bridgeCenterMagmaImpactPlayed_ && obj->GetWorldPosition().y <= bridgeMagmaEnterY) {
                        trans->translate.y = bridgeMagmaEnterY;
                        obj->UpdateWorldMatrix();
                        bridgeCenterMagmaImpactPlayed_ = true;
                        PlayBridgeMagmaSeIfNeeded();
                    }
                    if (!bridgeFallSe1Played_) {
                        audioPlayer_->PlaySE(seFallBridgeHandle_, false, SaveDataManager::GetInstance()->GetSEVolume());
                        bridgeFallSe1Played_ = true;
                    }
                }
                else if (movieTimer_ > 2.0f &&
                    obj->GetName() == "Bridge_Block_Back") {
                    // 少し遅れて奥のブロックもさらに崩れる
                    Transform* trans = obj->GetTransform();
                    const bool isSinking = bridgeBackMagmaImpactPlayed_;
                    trans->translate.y -= (isSinking ? 4.0f : 32.0f) * deltaTime;
                    trans->rotate.x += (isSinking ? 0.45f : 1.8f) * deltaTime; // 折れ曲がる
                    trans->isQuaternionMaster = false;
                    obj->UpdateWorldMatrix();
                    if (!bridgeBackMagmaImpactPlayed_ && obj->GetWorldPosition().y <= bridgeMagmaEnterY) {
                        trans->translate.y = bridgeMagmaEnterY;
                        obj->UpdateWorldMatrix();
                        bridgeBackMagmaImpactPlayed_ = true;
                        PlayBridgeMagmaSeIfNeeded();
                    }
                    if (!bridgeFallSe2Played_) {
                        audioPlayer_->PlaySE(seFallBridgeHandle_, false, SaveDataManager::GetInstance()->GetSEVolume());
                        bridgeFallSe2Played_ = true;
                    }
                }
                else if (movieTimer_ > 2.5f &&
                    obj->GetName() == "Bridge_Block_Front") {
                    // 最後に手前の親ブロックごと崩落する
                    Transform* trans = obj->GetTransform();
                    const bool isSinking = bridgeFrontMagmaImpactPlayed_;
                    trans->translate.y -= (isSinking ? 5.0f : 48.0f) * deltaTime;
                    trans->rotate.x += (isSinking ? 0.25f : 0.6f) * deltaTime;
                    trans->isQuaternionMaster = false;
                    obj->UpdateWorldMatrix();
                    if (!bridgeFrontMagmaImpactPlayed_ && obj->GetWorldPosition().y <= bridgeMagmaEnterY) {
                        trans->translate.y = bridgeMagmaEnterY;
                        obj->UpdateWorldMatrix();
                        bridgeFrontMagmaImpactPlayed_ = true;
                        PlayBridgeMagmaSeIfNeeded();
                    }
                    if (!bridgeFallSe3Played_) {
                        audioPlayer_->PlaySE(seFallBridgeHandle_, false, SaveDataManager::GetInstance()->GetSEVolume());
                        bridgeFallSe3Played_ = true;
                    }
                }
            }
        }

        // ムービー終了判定
        // (ブリッジブロックの物理的な落下演出自体はカメラが終わる頃まで続く想定)
        if (movieTimer_ >= 5.5f) {
#ifdef USE_IMGUI
            const bool isDebugBridgePreview = isBridgeDropPreviewForDebug_;
#else
            const bool isDebugBridgePreview = false;
#endif
            auto& objects_ref = objectManager_->GetObjects();
            for (auto& obj : objects_ref) {
                std::string name = obj->GetName();
                // 名前が "Bridge_"" で始まるオブジェクトを全て対象にする
                if (name.find("Bridge_") != std::string::npos) {
                    obj->SetCollisionAttribute(0); // 当たり判定を完全に消す
                    if (name.find("Bridge_Block") !=
                        std::string::npos) {    // ブリッジブロックは完全に消す
                        obj->SetIsVisible(false); // 見えなくする
                        obj->isDead = !isDebugBridgePreview; // プレビュー時は再生し直せるように保持する
                    }
                }
                else if (
                    name.find("Battle_Field_Collision_Box_South") !=
                    std::string::
                    npos) { // 南の当たり判定を復活させる（橋が落ちた後は通れなくする）
                    obj->SetCollisionAttribute(isDebugBridgePreview ? 0 : kGround);
                }
                else if (!isDebugBridgePreview && name.find("Tutorial_") != std::string::npos) {
                    if (name.find("Tutorial_Platform_02") != std::string::npos) {
                        continue;
                    }
                    else if (name.find("Tutorial_Platform_01") != std::string::npos) { // プラットフォーム装飾を確実に消す
                        auto& children = obj->GetChildren();
                        for (auto& child : children) {
                            child->SetCollisionAttribute(0); // 当たり判定を消す
                            child->SetIsVisible(false); // 見えなくする
                            child->isDead = true;       // 完全に消す（UpdateやDrawの対象から外す）
                        }
                    }
                    obj->SetCollisionAttribute(0); // 当たり判定を消す
                    obj->SetIsVisible(false); // 見えなくする
                    obj->isDead = true;       // 完全に消す（UpdateやDrawの対象から外す）
                }
            }
            StopBridgeMagmaSe();
            movieState_ = MovieState::kNone;
            player_->SetIsControlActive(true);
            player_->SetIsPhysicsActive(true);
            if (isDebugBridgePreview) {
                hasBridgeDropped_ = false;
                GameProgress::GetInstance()->hasBridgeDropped = false;
#ifdef USE_IMGUI
                isBridgeDropPreviewForDebug_ = false;
#endif
            } else {
                GameProgress::GetInstance()->hasBridgeDropped = true;
            }
        }

        // ムービー中は通常のプレイヤー入力やカメラ操作をスキップ
    }
    else if (movieState_ == MovieState::kTutorialDoorOpen) {
        // ムービー開始時の初期化
        if (movieTimer_ == 0.0f) {
            player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
            player_->SetIsControlActive(false);
            player_->SetIsPhysicsActive(false);
        }

        movieTimer_ += deltaTime;

        // 1.5秒後にムービー終了
        if (movieTimer_ > 2.5f) {
            movieState_ = MovieState::kNone;
            hasTutorialMovieFinished_ = true; // ムービー終了

            // 扉が開いた後、WindのBGMを再生
            bgmHandle_ = bgmWindHandle_;
            audioPlayer_->PlayBGM(bgmHandle_, true, SaveDataManager::GetInstance()->GetBGMVolume() / 2);

            missionSwitchDelayTimer_ = 0.5f;  // 0.5秒待機
            player_->SetIsControlActive(true);
            player_->SetIsPhysicsActive(true);
            Camera* camera = CameraManager::GetInstance()->GetMainCamera();
            camera->EndOverride(1.5f);
        }
    }
    else if (movieState_ == MovieState::kTutorialPlatformDescent) {
        if (tutorialPlatform_ && player_) {
            player_->SetIsControlActive(false);
            player_->SetIsPhysicsActive(false);

            Transform* trans = tutorialPlatform_->GetTransform();
            if (trans->translate.y > 29.6f) {
                trans->translate.y -= 15.0f * deltaTime;
                if (trans->translate.y < 29.6f)
                    trans->translate.y = 29.6f;
                tutorialPlatform_->UpdateWorldMatrix();
            }
            else {
                // 到着 movieState_ が kNone
                // になるので、下のシャッター制御が「下げ」に転じます
                movieState_ = MovieState::kNone;
                missionSwitchDelayTimer_ = 0.5f; // 0.5秒待機
                player_->SetIsControlActive(true);
                player_->SetIsPhysicsActive(true); // 物理復帰

                // エレベーター降下SEの停止
                audioPlayer_->StopSe(seElevatorHandle_);
                if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                    camera->EndOverride(1.5f);
                }

                if (!hasFinishedTutorial_) {
                    tutorialStep_ = TutorialStep::kShowMove;
                    tutorialTimer_ = 0.0f;
                    if (tutorialMoveSprite_) {
                        Vector4 c = tutorialMoveSprite_->GetColor();
                        c.w = 1.0f;
                        tutorialMoveSprite_->SetColor(c);
                    }
                }

                if (!missionInitialShown_) {
                    missionInitialShown_ = true;
                }
            }
            player_->GetTransform()->translate.y =
                trans->translate.y + tutorialPlatformOffset_;
            player_->UpdateWorldMatrix();
        }
    }
}

void GamePlayScene::UpdateTutorialGuide(float deltaTime) {
    // ドアが開いて一定時間経過したらチュートリアルUIを強制非表示
    if (doorOpenProgress_ >= 1.0f && !tutorialUiCompleted_) {
        doorOpenedTimer_ += deltaTime;
        if (doorOpenedTimer_ > 3.0f) {
            auto HideSprite = [](Sprite* s) {
                if (s) {
                    Vector4 c = s->GetColor();
                    c.w = 0.0f;
                    s->SetColor(c);
                }
            };
            HideSprite(tutorialMoveSprite_);
            HideSprite(tutorialCameraSprite_);
            HideSprite(tutorialJumpSprite_);
            HideSprite(tutorialLockOnSprite_);
            HideSprite(tutorialAttackSprite_);
            HideSprite(tutorialFallAttackSprite_);
            HideSprite(tutorialDodgeSprite_);

            tutorialStep_ = TutorialStep::kCompleted;
            tutorialUiCompleted_ = true;
        }
    }

    // チュートリアル状態機（順序：移動 → カメラ → ロックオン → 攻撃 → 回避）
    if (!tutorialUiCompleted_) {
        switch (tutorialStep_) {
        case TutorialStep::kShowMove:
            // 表示済みを確認して入力待ちへ
            tutorialStep_ = TutorialStep::kWaitForMove;
            tutorialTimer_ = 0.0f;
            break;

        case TutorialStep::kWaitForMove: {
            bool moved = false;
            if (inputManager_) {
                Vector2 left = inputManager_->GetLeftStick();
                if (std::abs(left.x) > 0.2f || std::abs(left.y) > 0.2f)
                    moved = true;
                if (inputManager_->IsKeyPressed(DIK_W) ||
                    inputManager_->IsKeyPressed(DIK_A) ||
                    inputManager_->IsKeyPressed(DIK_S) ||
                    inputManager_->IsKeyPressed(DIK_D)) {
                    moved = true;
                }
            }
            if (!moved && player_) {
                Vector3 vel = player_->GetVelocity();
                float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
                if (speed > 0.1f)
                    moved = true;
            }
            if (moved) {
                tutorialMoveTimer_ += deltaTime;
            }
            if (tutorialMoveTimer_ >= 1.5f) {
                // 次のカメラ説明表示へ切り替え
                if (tutorialMoveSprite_) {
                    Vector4 c = tutorialMoveSprite_->GetColor();
                    c.w = 0.0f;
                    tutorialMoveSprite_->SetColor(c);
                }
                if (tutorialCameraSprite_) {
                    Vector4 c = tutorialCameraSprite_->GetColor();
                    c.w = 1.0f;
                    tutorialCameraSprite_->SetColor(c);
                }
                tutorialStep_ = TutorialStep::kWaitForCamera;
                tutorialTimer_ = 0.0f;
            }
        } break;

        case TutorialStep::kWaitForCamera: {
            bool cameraUsed = false;
            if (inputManager_) {
                Vector2 right = inputManager_->GetRightStick();
                Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
                if (std::abs(right.x) > 0.2f || std::abs(right.y) > 0.2f)
                    cameraUsed = true;
                if (std::abs(mouseDelta.x) > 2.0f || std::abs(mouseDelta.y) > 2.0f)
                    cameraUsed = true;
            }
            if (cameraUsed) {
                tutorialCameraTimer_ += deltaTime;
            }
            if (tutorialCameraTimer_ >= 1.0f) {
                if (tutorialCameraSprite_) {
                    Vector4 c = tutorialCameraSprite_->GetColor();
                    c.w = 0.0f;
                    tutorialCameraSprite_->SetColor(c);
                }
                if (tutorialJumpSprite_) {
                    Vector4 c = tutorialJumpSprite_->GetColor();
                    c.w = 1.0f;
                    tutorialJumpSprite_->SetColor(c);
                }
                tutorialStep_ = TutorialStep::kWaitForJump;
                tutorialTimer_ = 0.0f;
            }
        } break;

        case TutorialStep::kWaitForJump:
            if (inputManager_ && inputManager_->IsActionTriggered("Jump")) {
                tutorialJumpCount_++;
                if (tutorialJumpCount_ >= 2) {
                    if (tutorialJumpSprite_) {
                        Vector4 c = tutorialJumpSprite_->GetColor();
                        c.w = 0.0f;
                        tutorialJumpSprite_->SetColor(c);
                    }
                    if (tutorialLockOnSprite_) {
                        Vector4 c = tutorialLockOnSprite_->GetColor();
                        c.w = 1.0f;
                        tutorialLockOnSprite_->SetColor(c);
                    }
                    tutorialStep_ = TutorialStep::kWaitForLockOn;
                    tutorialTimer_ = 0.0f;
                }
            }
            break;

        case TutorialStep::kWaitForLockOn:
            if (lockOnSystem_ && lockOnSystem_->IsLockingOn()) {
                if (tutorialLockOnSprite_) {
                    Vector4 c = tutorialLockOnSprite_->GetColor();
                    c.w = 0.0f;
                    tutorialLockOnSprite_->SetColor(c);
                }
                if (tutorialAttackSprite_) {
                    Vector4 c = tutorialAttackSprite_->GetColor();
                    c.w = 1.0f;
                    tutorialAttackSprite_->SetColor(c);
                }
                tutorialStep_ = TutorialStep::kWaitForAttack;
                tutorialTimer_ = 0.0f;
            }
            break;

        case TutorialStep::kWaitForAttack: {
            static float attackCooldown = 0.0f;
            if (attackCooldown > 0.0f) attackCooldown -= deltaTime;
            if (inputManager_) {
                // 攻撃ボタン検出（KeyConfig の "Attack" に対応）
                if (inputManager_->IsActionTriggered("Attack") && attackCooldown <= 0.0f) {
                    tutorialAttackCount_++;
                    attackCooldown = 0.5f;
                    if (tutorialAttackCount_ >= 2) {
                        if (tutorialAttackSprite_) {
                            Vector4 c = tutorialAttackSprite_->GetColor();
                            c.w = 0.0f;
                            tutorialAttackSprite_->SetColor(c);
                        }
                        if (tutorialFallAttackSprite_) {
                            Vector4 c = tutorialFallAttackSprite_->GetColor();
                            c.w = 1.0f;
                            tutorialFallAttackSprite_->SetColor(c);
                        }
                        tutorialStep_ = TutorialStep::kWaitForFallAttack;
                        tutorialTimer_ = 0.0f;
                    }
                }
            }
        } break;

        case TutorialStep::kWaitForFallAttack: {
            static float fallAttackCooldown = 0.0f;
            if (fallAttackCooldown > 0.0f) fallAttackCooldown -= deltaTime;
            if (inputManager_ && player_) {
                bool isFalling = (player_->GetVelocity().y < -0.1f);
                if (inputManager_->IsActionTriggered("Attack") && isFalling && fallAttackCooldown <= 0.0f) {
                    tutorialFallAttackCount_++;
                    fallAttackCooldown = 0.5f;
                    if (tutorialFallAttackCount_ >= 2) {
                        if (tutorialFallAttackSprite_) {
                            Vector4 c = tutorialFallAttackSprite_->GetColor();
                            c.w = 0.0f;
                            tutorialFallAttackSprite_->SetColor(c);
                        }
                        if (tutorialDodgeSprite_) {
                            Vector4 c = tutorialDodgeSprite_->GetColor();
                            c.w = 1.0f;
                            tutorialDodgeSprite_->SetColor(c);
                        }
                        tutorialStep_ = TutorialStep::kWaitForDodge;
                        tutorialTimer_ = 0.0f;
                    }
                }
            }
        } break;

        case TutorialStep::kWaitForDodge:
            if (inputManager_) {
                // ダッシュがトリガーされたか
                bool dashTriggered = inputManager_->IsActionTriggered("Dash");

                // 同時に移動入力があるかをチェック（左スティックまたはW/A/S/D、または速度による判定）
                bool moveInput = false;
                Vector2 left = inputManager_->GetLeftStick();
                if (std::abs(left.x) > 0.2f || std::abs(left.y) > 0.2f)
                    moveInput = true;
                if (inputManager_->IsKeyPressed(DIK_W) ||
                    inputManager_->IsKeyPressed(DIK_A) ||
                    inputManager_->IsKeyPressed(DIK_S) ||
                    inputManager_->IsKeyPressed(DIK_D)) {
                    moveInput = true;
                }
                if (!moveInput && player_) {
                    Vector3 vel = player_->GetVelocity();
                    float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
                    if (speed > 0.1f)
                        moveInput = true;
                }

                // ダッシュかつ移動入力がある場合に回避完了とする
                if (dashTriggered && moveInput) {
                    if (tutorialDodgeSprite_) {
                        Vector4 c = tutorialDodgeSprite_->GetColor();
                        c.w = 0.0f;
                        tutorialDodgeSprite_->SetColor(c);
                    }
                    tutorialStep_ = TutorialStep::kCompleted;
                    tutorialUiCompleted_ = true;
                    // 必要なら hasFinishedTutorial_ をここで true にする
                }
            }
            break;

        default:
            break;
        }
    }

    // =======================================================
    // チュートリアル矢印の演出処理
    // =======================================================
    if (tutorialArrow_) {
        Transform* trans = tutorialArrow_->GetTransform();
        
        if (doorOpenProgress_ < 1.0f) {
            tutorialArrowAnimTimer_ += deltaTime;
            
            // 1. 上下運動 (サイン波でふわふわ)
            float hover = std::sin(tutorialArrowAnimTimer_ * 3.0f) * 0.3f;
            
            // 2. 定期的なY軸回転 (イージング)
            float cycle = std::fmod(tutorialArrowAnimTimer_, 4.0f); // 4秒周期
            float rotY = 0.0f;
            if (cycle < 1.0f) {
                // 0~1秒の間に360度回転させる (EaseInOutCubic)
                float t = cycle;
                float easeT = t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
                rotY = easeT * 3.14159265f * 2.0f;
            } else {
                rotY = 0.0f; // 回転完了後待機
            }
            
            trans->translate = tutorialArrowDefaultPos_ + Vector3{0.0f, hover, 0.0f};
            trans->rotate = {0.0f, rotY, 0.0f};
        } else {
            // 3. ドアが開いた後の道案内ロジック
            
            // ウェイポイントの定義
            std::vector<Vector3> waypoints = {
                {0.0f, 24.0f, -200.0f},
                {0.3f, 24.0f, -153.0f}
            };
            
            if (tutorialArrowWaypointIndex_ <= waypoints.size()) {
                Vector3 targetPos;
                bool isFinalTarget = false;
                
                if (tutorialArrowWaypointIndex_ < waypoints.size()) {
                    targetPos = waypoints[tutorialArrowWaypointIndex_];
                } else if (boss_) {
                    targetPos = boss_->GetWorldPosition();
                    targetPos.y += 2.0f; // ボスコアの少し上を狙う
                    isFinalTarget = true;
                } else {
                    tutorialArrow_->SetIsVisible(false);
                    tutorialArrowWaypointIndex_ = 999;
                    targetPos = tutorialArrowDefaultPos_;
                }
                
                Vector3 dir = {
                    targetPos.x - tutorialArrowDefaultPos_.x,
                    targetPos.y - tutorialArrowDefaultPos_.y,
                    targetPos.z - tutorialArrowDefaultPos_.z
                };
                
                float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                
                if (dist < 2.0f) {
                    if (isFinalTarget) {
                        // ボスコアに着いたら消す
                        tutorialArrow_->SetIsVisible(false);
                        tutorialArrowWaypointIndex_ = 999;
                    } else {
                        // 中継ポイントでプレイヤーが近づくのを待つ
                        // 待機時はX軸の回転を-90度に固定し、次の目的地の方角(Y軸)を向くようにする
                        trans->rotate.x = -90.0f * (3.14159265f / 180.0f);
                        
                        Vector3 nextTargetPos;
                        bool hasNextTarget = false;
                        if (tutorialArrowWaypointIndex_ + 1 < waypoints.size()) {
                            nextTargetPos = waypoints[tutorialArrowWaypointIndex_ + 1];
                            hasNextTarget = true;
                        } else if (boss_) {
                            nextTargetPos = boss_->GetWorldPosition();
                            nextTargetPos.y += 2.0f;
                            hasNextTarget = true;
                        }
                        
                        if (hasNextTarget) {
                            float nx = nextTargetPos.x - tutorialArrowDefaultPos_.x;
                            float nz = nextTargetPos.z - tutorialArrowDefaultPos_.z;
                            trans->rotate.y = std::atan2(nx, nz);
                        }
                        
                        if (player_) {
                            Vector3 playerPos = player_->GetWorldPosition();
                            float pDistX = playerPos.x - tutorialArrowDefaultPos_.x;
                            float pDistY = playerPos.y - tutorialArrowDefaultPos_.y;
                            float pDistZ = playerPos.z - tutorialArrowDefaultPos_.z;
                            float distToPlayer = std::sqrt(pDistX * pDistX + pDistY * pDistY + pDistZ * pDistZ);
                            
                            // プレイヤーがある程度近づいたら次のポイントへ進む
                            if (distToPlayer < 10.0f) {
                                tutorialArrowWaypointIndex_++;
                            }
                        }
                    }
                } else {
                    // ターゲットに向かって速く移動する
                    float speed = 80.0f; // 移動速度アップ
                    dir.x /= dist;
                    dir.y /= dist;
                    dir.z /= dist;
                    
                    float moveDist = speed * deltaTime;
                    if (moveDist > dist) moveDist = dist; // 行き過ぎ防止
                    
                    tutorialArrowDefaultPos_.x += dir.x * moveDist;
                    tutorialArrowDefaultPos_.y += dir.y * moveDist;
                    tutorialArrowDefaultPos_.z += dir.z * moveDist;
                    
                    // 矢印の向きを進行方向に合わせる
                    float distXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                    float targetRotY = std::atan2(dir.x, dir.z);
                    float targetRotX = -std::atan2(dir.y, distXZ);
                    
                    // モデルの元々の向き(上向きを想定)を考慮し、X軸に-90度(-PI/2)のオフセットを加える
                    trans->rotate = { targetRotX - (3.14159265f / 2.0f), targetRotY, 0.0f };
                }
            }
            trans->translate = tutorialArrowDefaultPos_;
        }

        trans->isQuaternionMaster = false;
        tutorialArrow_->UpdateWorldMatrix();
    }
}

void GamePlayScene::UpdateLockOnAndCamera(float deltaTime, bool isCinematicMode, Camera* camera, Math& math) {
    // --- ロックオン & カメラ制御 ---
    lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_);

    bool isLockingOn = lockOnSystem_->IsLockingOn();
    if (isLockingOn && !wasLockingOn_) {
        audioPlayer_->PlaySE(seLockOnHandle_, false, SaveDataManager::GetInstance()->GetSEVolume());
    }
    wasLockingOn_ = isLockingOn;

    CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());
    // =================================================================
    // ロックオンアイコンの 2.5D 追従計算 (World To Screen)
    // =================================================================
    Object3d* target = lockOnSystem_->GetTarget();

    if (target && lockOnSystem_->IsLockingOn()) {
        isDrawLockOn_ = true;

        // =======================================================
        // ：AABB(当たり判定)から「真の中心」と「大きさ」を取得
        // =======================================================
        AABB aabb = target->GetAABB();

        // ① ターゲットの「真の中心座標」を計算
        Vector3 targetCenter;
        targetCenter.x = (aabb.min.x + aabb.max.x) * 0.5f;
        targetCenter.y = (aabb.min.y + aabb.max.y) * 0.5f;
        targetCenter.z = (aabb.min.z + aabb.max.z) * 0.5f;

        // ② カメラのビュー行列とプロジェクション行列を掛け合わせる
        Matrix4x4 viewProj =
            math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());

        // ③ ワールド座標(中心) → クリップ座標 (W除算) の計算
        float w = targetCenter.x * viewProj.m[0][3] +
            targetCenter.y * viewProj.m[1][3] +
            targetCenter.z * viewProj.m[2][3] + viewProj.m[3][3];

        // カメラの後ろ（画面外）にいる時は表示しない
        if (w > 0.001f) {
            Vector3 ndc;
            ndc.x = (targetCenter.x * viewProj.m[0][0] +
                targetCenter.y * viewProj.m[1][0] +
                targetCenter.z * viewProj.m[2][0] + viewProj.m[3][0]) /
                w;
            ndc.y = (targetCenter.x * viewProj.m[0][1] +
                targetCenter.y * viewProj.m[1][1] +
                targetCenter.z * viewProj.m[2][1] + viewProj.m[3][1]) /
                w;
            float screenWidth = static_cast<float>(WinApp::kClientWidth);
            float screenHeight = static_cast<float>(WinApp::kClientHeight);

            float screenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
            float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;

            lockOnSprite_->SetPosition({ screenX, screenY });

            // =======================================================
            // ：オブジェクトの大きさに応じたアイコンサイズの自動調整
            // =======================================================
            float objSizeX = aabb.max.x - aabb.min.x;
            float objSizeY = aabb.max.y - aabb.min.y;
            float objSizeZ = aabb.max.z - aabb.min.z;
            float maxObjSize = std::max({ objSizeX, objSizeY, objSizeZ });

            float baseSize = maxObjSize * 25.0f;
            float distanceScale = 20.0f / w;

            float finalSize = baseSize * distanceScale;
            finalSize = std::max(32.0f, std::min(finalSize, 256.0f));

            lockOnSprite_->SetSize({ finalSize, finalSize });

            // （おまけ）ロックオンアイコンを毎フレーム少し回転させると超カッコよくなります
            float currentRot = lockOnSprite_->GetRotation();
            lockOnSprite_->SetRotation(currentRot + 2.0f * deltaTime);

            lockOnSprite_->Update();
        }
        else {
            isDrawLockOn_ = false; // カメラの裏にいる時は消す
        }
    }
    else {
        // =======================================================
        // ロックオンしていない時は確実に表示をオフにする
        // =======================================================
        isDrawLockOn_ = false;
    }

    // 自由カメラモード以外の操作
    const bool isCameraOverrideActive =
        camera && (camera->IsOverridden() || camera->GetOverrideWeight() > 0.001f);
    const bool isCameraReturnLocked =
        isCameraOverrideActive || CameraEditor::GetInstance()->IsCinematicReturnInputLocked();
    if (!CameraEditor::GetInstance()->IsEditorMode() && !isCinematicMode && !isCameraReturnLocked) {
        Camera::FollowMode currentMode = camera->GetFollowMode();

        if (player_ && player_->GetHp() > 0.0f &&
            (currentMode == Camera::FollowMode::kAimable ||
                currentMode == Camera::FollowMode::kFirstPerson)) {

            // =======================================================
            // 1. マウスの移動量と、ゲームパッドの右スティック入力を両方取得
            // =======================================================
            Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
            Vector2 rightStick = inputManager_->GetRightStick();

            // =======================================================
            // 2. 入力値から「最終的な移動量」を出す（感度はCamera側で処理）
            // =======================================================
            // 感度曲線（3乗）を適用し、細かいエイム調整と素早い旋回を両立
            float stickX = rightStick.x * rightStick.x * rightStick.x;
            float stickY = rightStick.y * rightStick.y * rightStick.y;

            // スティック入力にはdeltaTimeを掛けることでフレームレート（FPS）に依存しない速度にする
            // マウス（mouseDelta）は1フレームあたりのピクセル移動量そのものなのでそのまま加算します。
            Vector2 totalDelta;
            totalDelta.x = mouseDelta.x + (stickX * 900.0f * deltaTime);
            totalDelta.y = mouseDelta.y - (stickY * 900.0f * deltaTime); // スティックの上下は反転

#ifdef USE_IMGUI
            // デバッグ(Develop)環境:
            // UI操作の誤爆を防ぐため「右クリック中」または「スティック入力中」のみ回転
            if (inputManager_->IsMouseButtonPressed(1) || rightStick.x != 0.0f ||
                rightStick.y != 0.0f) {
                if (totalDelta.x != 0.0f || totalDelta.y != 0.0f) {
                    camera->AddRotation(totalDelta);
                }
            }
#else
            // Release環境限定: 右クリック不要操作した分だけ回転する
            if (totalDelta.x != 0.0f || totalDelta.y != 0.0f) {
                camera->AddRotation(totalDelta);
            }
#endif
        }
    }
    // ムービー状態などに応じて黒帯の高さを決める
    float targetBarHeight = isCinematicMode ? 0.12f : 0.0f;

    // 現在の高さを滑らかに補間（5.0f は開閉スピード）
    currentCinemaBarHeight_ +=
        (targetBarHeight - currentCinemaBarHeight_) * 5.0f * deltaTime;

    // ポストエフェクトに反映
    PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
    if (postParams) {
        postParams->cinemaBarHeight = currentCinemaBarHeight_;
    }
}

void GamePlayScene::UpdateSceneObjects(float deltaTime) {
    // --- 全体更新 ---
    CameraManager::GetInstance()->Update();
    particleSystem_->Update(deltaTime);
    objectManager_->Update(deltaTime); // オブジェクト一括更新

    if (boss_) {
        boss_->ActuallySpawnShards();
    }

    if (timeAttackUI_) {
        timeAttackUI_->Update(deltaTime);
    }
    //// 溜まった発生命令をもとに、GPUに計算（Compute Shader）を走らせる
    GPUParticleManager::GetInstance()->Update(deltaTime);
    for (auto& sprite : sprites_) {
        sprite->Update();
    }
}

void GamePlayScene::UpdateGameOver(float originalDeltaTime) {
    // =========================================================
    // ゲームオーバー画面のフェードインとメニュー選択
    // =========================================================
    if (player_ && player_->GetHp() <= 0.0f) {

        // プレイヤーの点滅演出(3.5秒)が終わったら処理開始
        if (player_->GetDeathTimer() > 3.5f) {

            // BGMを敗北曲（defeat.mp3）に切り替える
            if (bgmHandle_ != bgmDefeatHandle_) {
                bgmHandle_ = bgmDefeatHandle_;
                audioPlayer_->PlayBGM(bgmHandle_, true, SaveDataManager::GetInstance()->GetBGMVolume());
            }

            if (!isGameOverUiStarted_) {
                isGameOverUiStarted_ = true;
                ResetGameOverUiVisuals();
            }

            gameOverUiTimer_ += originalDeltaTime;
            const float titleAlpha = Clamp01(gameOverUiTimer_ / 0.75f);
            const float menuAlpha = Clamp01((gameOverUiTimer_ - 0.45f) / 0.55f);

            if (gameOverUiTimer_ >= 1.05f) {
                isGameOverUiReady_ = true;
            }

            if (isGameOverUiReady_) {
                InputManager* input = InputManager::GetInstance();

                bool cursorMoved = false;
                if (input->IsActionTriggered("Forward")) {
                    currentGameOverMenuIndex_--;
                    if (currentGameOverMenuIndex_ < 0) {
                        currentGameOverMenuIndex_ = (int)GameOverMenuIndex::Max - 1;
                    }
                    cursorMoved = true;
                }
                if (input->IsActionTriggered("Backward")) {
                    currentGameOverMenuIndex_++;
                    if (currentGameOverMenuIndex_ >= (int)GameOverMenuIndex::Max) {
                        currentGameOverMenuIndex_ = 0;
                    }
                    cursorMoved = true;
                }
                if (cursorMoved) {
                    audioPlayer_->PlaySE(seCursorMove_, false, 1.0f);
                }

                if (input->IsActionTriggered("Jump")) {
                    audioPlayer_->PlaySE(seDecide_, false, 1.0f);
                    if (currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Restart) {
                        isRestartTransition_ = true;
                        restartTimer_ = 0.0f;
                        HideGameOverUi();
                    }
                    else if (currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Title) {
                        isTitleTransition_ = true;
                        restartTimer_ = 0.0f;
                        HideGameOverUi();
                    }
                }
            }

            UpdateGameOverTitleGlyphs(originalDeltaTime, titleAlpha);
            UpdateGameOverMenuVisuals(originalDeltaTime, menuAlpha);
            return;

            // --- 1. テキストのフェードイン ---
            if (!isGameOverUiReady_) {
                bool allFadedIn = true;

                auto FadeInSprite = [originalDeltaTime, &allFadedIn](Sprite* sprite) {
                    if (sprite) {
                        Vector4 color = sprite->GetColor();
                        if (color.w < 1.0f) {
                            color.w += originalDeltaTime * 0.5f; // 徐々に不透明にする
                            if (color.w > 1.0f)
                                color.w = 1.0f;
                            sprite->SetColor(color);
                            allFadedIn = false; // まだ透明なやつがいればフラグを下ろす
                        }
                    }
                    };

                FadeInSprite(gameOverTextSprite_);
                FadeInSprite(restartTextSprite_);
                FadeInSprite(titleTextSprite_);

                // 全部の文字が完全に出現したら準備完了
                if (allFadedIn) {
                    isGameOverUiReady_ = true;
                }
            }
            // --- 2. メニュー選択とシーン遷移 ---
            else {
                InputManager* input = InputManager::GetInstance();

                bool cursorMoved = false;
                // 上下キーで項目切り替え (パッドの十字キーにも対応)
                if (input->IsActionTriggered("Forward")) {
                    currentGameOverMenuIndex_--;
                    if (currentGameOverMenuIndex_ < 0)
                        currentGameOverMenuIndex_ = (int)GameOverMenuIndex::Max - 1;
                    cursorMoved = true;
                }
                if (input->IsActionTriggered("Backward")) {
                    currentGameOverMenuIndex_++;
                    if (currentGameOverMenuIndex_ >= (int)GameOverMenuIndex::Max)
                        currentGameOverMenuIndex_ = 0;
                    cursorMoved = true;
                }
                if (cursorMoved) {
                    audioPlayer_->PlaySE(seCursorMove_, false, 1.0f);
                }

                // 選択中の項目をハイライト
                // (選ばれてるほうを白、そうでないほうを少し暗くする)
                Vector4 normalColor = { 0.5f, 0.5f, 0.5f, 1.0f };
                Vector4 selectColor = { 1.0f, 1.0f, 1.0f, 1.0f };

                if (restartTextSprite_) {
                    restartTextSprite_->SetColor(currentGameOverMenuIndex_ ==
                        (int)GameOverMenuIndex::Restart
                        ? selectColor
                        : normalColor);
                }
                if (titleTextSprite_) {
                    titleTextSprite_->SetColor(currentGameOverMenuIndex_ ==
                        (int)GameOverMenuIndex::Title
                        ? selectColor
                        : normalColor);
                }
                // 決定ボタンで遷移
                if (input->IsActionTriggered("Jump")) {
                    audioPlayer_->PlaySE(seDecide_, false, 1.0f);

                    // 共通のUI透明化ラムダ式
                    auto SetAlphaZero = [](Sprite* sprite) {
                        if (sprite) {
                            Vector4 color = sprite->GetColor();
                            color.w = 0.0f;
                            sprite->SetColor(color);
                        }
                        };

                    if (currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Restart) {
                        isRestartTransition_ = true;
                        restartTimer_ = 0.0f;

                        SetAlphaZero(gameOverTextSprite_);
                        SetAlphaZero(restartTextSprite_);
                        SetAlphaZero(titleTextSprite_);
                    }
                    else if (currentGameOverMenuIndex_ ==
                        (int)GameOverMenuIndex::Title) {

                        isTitleTransition_ = true;
                        restartTimer_ = 0.0f;

                        SetAlphaZero(gameOverTextSprite_);
                        SetAlphaZero(restartTextSprite_);
                        SetAlphaZero(titleTextSprite_);
                    }
                }
            }
        }
    }
}

void GamePlayScene::UpdateGameplaySystems(float deltaTime) {
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
    MeshEffectManager::GetInstance()->Update(deltaTime);
    UpdateUI(deltaTime);
}

void GamePlayScene::UpdateBossMovie(float deltaTime) {
    // ========================================================
    // ボス登場ムービー中の監視処理（時間で強制終了）
    // ========================================================
    if (isBossMoviePlaying_ && boss_) {

        // タイマーを進める
        movieTimer_ += deltaTime;

        // プレイヤーがズレないように固定し続ける
        if (player_) {
            player_->SetTranslate(movieStoredPlayerPos_);
            player_->UpdateWorldMatrix();
        }

        // ====================================================
        // 全体時間を 3.0f から 4.0f に伸ばす（1秒の待機が増えたため）
        // ====================================================
        if (movieTimer_ >= 4.0f) {
            isBossMoviePlaying_ = false;
            missionSwitchDelayTimer_ = 0.5f; // 0.5秒待機

            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                camera->EndOverride(1.0f);
            }

            if (player_) {
                player_->SetIsControlActive(true);
                player_->SetIsPhysicsActive(true);
            }

            // BGMを戦闘曲(battle_01.mp3)に切り替えて再生
            bgmHandle_ = bgmBattle01Handle_;
            audioPlayer_->PlayBGM(bgmHandle_, true, SaveDataManager::GetInstance()->GetBGMVolume());

            boss_->StartBattle();

            // ボス登場後に missionText_boss を表示（1回だけ）
            if (!missionBossShown_ && missionText_boss_) {
                missionBossShown_ = true;
            }

            if (timeAttackUI_) {
                timeAttackUI_->Start();
            }
        }
    }
}

void GamePlayScene::UpdateClearSequence(float deltaTime) {
    if (boss_) {
        // ボスが完全に消滅し、かつまだクリアシーケンスに入っていなければ開始
        if (boss_->IsCompletelyDead() && !isGameClearSequence_) {
            isGameClearSequence_ = true;
            audioPlayer_->PlaySE(seMissionClear3Handle_, false, SaveDataManager::GetInstance()->GetSEVolume());
            gameClearTimer_ = 0.0f;

            // タイマーを止める
            if (timeAttackUI_) {
                timeAttackUI_->Stop();
            }
            float clearTime = timeAttackUI_->GetCurrentTime();
            SaveDataManager::GetInstance()->RecordClearTime(clearTime);

            DebugConsole::GetInstance()->AddLog(
                "クリアタイムを保存しました: " + std::to_string(clearTime) + " 秒");
            DebugConsole::GetInstance()->AddLog("【GAME CLEAR】 クリア演出開始！");
        }
    }

    // クリアシーケンス中の処理
    if (isGameClearSequence_) {
        gameClearTimer_ += deltaTime;

        // ボス消滅から 2.0 秒後に「CLEAR」シーンへ遷移
        if (gameClearTimer_ > 2.0f) {
            SceneManager::GetInstance()->ChangeScene("GAMECLEAR");
        }
    }
}


void GamePlayScene::Draw() {
    // --- 一人称視点判定 ---
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
#ifndef _DEBUG
    if (camera->GetFollowTarget() &&
        camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    // =========================================================

    // カメラがプレイヤーに近すぎたら、強制的に「非表示(一人称扱い)」にする
    // =========================================================
    if (!isFirstPerson && player_ && camera) {
        Vector3 pPos = player_->GetWorldPosition();
        pPos.y += 1.0f; // プレイヤーの胸の高さを基準にする
        Vector3 cPos = camera->GetEye();
        Vector3 toCam = { cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z };
        float dist =
            std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);

        // 距離が 3.0m 未満なら、プレイヤーを完全に消す
        if (dist < 3.0f) {
            isFirstPerson = true;
        }
    }

    ID3D12Resource* pointLightRes =
        LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes =
        LightManager::GetInstance()->GetSpotLightResource();
    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    // =========================================================
    // ここに「完全自動カリング」のロジックを挿入
    // =========================================================
    Frustum frustum = camera->GetFrustum();
    Math math;
    int drawCount = 0;
    int totalCount = 0;

    auto IsVisible = [&](Object3d* obj) {
        if (!obj->GetIsVisible())
            return false;

        // 1. オブジェクトから Model を取得
        // Object3d に GetModel() が必要 (return
        // model_; など )
        Model* model = obj->GetModel();
        if (!model)
            return true; // モデルが無い(空の)場合は安全のため描画を通す

        // 2. モデル本来のサイズ（ローカルAABB）を取得
        Vector3 lMin = model->GetLocalAabbMin();
        Vector3 lMax = model->GetLocalAabbMax();

        // 3. ローカルの「箱の8つの角（頂点）」を作成
        Vector3 corners[8] = { {lMin.x, lMin.y, lMin.z}, {lMax.x, lMin.y, lMin.z},
                              {lMin.x, lMax.y, lMin.z}, {lMax.x, lMax.y, lMin.z},
                              {lMin.x, lMin.y, lMax.z}, {lMax.x, lMin.y, lMax.z},
                              {lMin.x, lMax.y, lMax.z}, {lMax.x, lMax.y, lMax.z} };

        // 4. ワールド行列を使って、8つの角すべてをゲーム空間の座標に変換する
        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wMin = { FLT_MAX, FLT_MAX, FLT_MAX };
        Vector3 wMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

        for (int i = 0; i < 8; ++i) {
            // math.Transform で座標に行列を掛ける
            Vector3 wPos = math.Transform(corners[i], wm);

            // 変換後の8つの点から、ワールド空間での新たな min / max を見つける
            wMin.x = (std::min)(wMin.x, wPos.x);
            wMin.y = (std::min)(wMin.y, wPos.y);
            wMin.z = (std::min)(wMin.z, wPos.z);
            wMax.x = (std::max)(wMax.x, wPos.x);
            wMax.y = (std::max)(wMax.y, wPos.y);
            wMax.z = (std::max)(wMax.z, wPos.z);
        }

        // 回転したことで箱が大きくなっても問題なし確実にオブジェクトを包み込むAABBが完成。
        return math.IntersectFrustumAABB(frustum, wMin, wMax);
        };

    // --- 1. 不透明描画 ---
    for (auto& obj : objects) {

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
            continue; // プレイヤーの一部なら描画をスキップ

        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 ||
            obj->GetMaterialType() == 10 || obj->GetMaterialType() == 11)
            continue;

        totalCount++;
        // カリング判定
        if (IsVisible(obj.get())) {
            obj->Draw(pointLightRes, spotLightRes);
            drawCount++;
        }
    }

    // --- 2. 中間描画 (弾・デバッグ) ---
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    if (debugEditor_)
        debugEditor_->DrawPreview(pointLightResource_.Get(),
            spotLightResource_.Get());
    LightEditor::GetInstance()->Draw3D();
    MeshEffectManager::GetInstance()->Draw(pointLightRes, spotLightRes);

    // --- 3. 透明描画 ---
    for (auto& obj : objects) {
        // ここでも同じくプレイヤー関連をスキップ
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

        if (obj->GetMaterialType() == 1) { // 透明のみ描画
            totalCount++;
            // カリング判定
            if (IsVisible(obj.get())) {
                obj->Draw(pointLightRes, spotLightRes);
                drawCount++;
            }
        }
    }
    particleSystem_->Draw();

    // =======================================================
    // 4. ローカルフォグ (霧の箱) の描画
    // =======================================================
    bool hasFog = false;
    for (auto& obj : objects) {
        if (obj->GetMaterialType() == 7)
            hasFog = true;
    }

    if (hasFog) {
        dxCommon_->PreDrawLocalFog();
        for (auto& obj : objects) {
            if (obj->GetMaterialType() == 7) {
                // フォグの箱自体も画面外なら描画しないように最適化
                if (IsVisible(obj.get())) {
                    obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
                }
            }
        }
        dxCommon_->PostDrawLocalFog();
    }

    // =======================================================
    // 5. GPUパーティクルの描画
    // =======================================================
    dxCommon_->UpdateGrabTexture();

    for (auto& obj : objects) {
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

        if (obj->GetMaterialType() == 10 && IsVisible(obj.get())) {
            obj->DrawWater(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
        } else if (obj->GetMaterialType() == 11 && IsVisible(obj.get())) {
            obj->DrawMagma(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
        }
    }

    GPUParticleManager::GetInstance()->Draw(
        dxCommon_->GetCommandList(), camera->GetViewMatrix(),
        camera->GetProjectionMatrix(), gpuParticleTexHandle_,
        dxCommon_->GetDepthSrvHandle());

    // カリングがどれくらい効いているか確認用のログ
    // DebugConsole::GetInstance()->AddLog("DrawCount: " +
    // std::to_string(drawCount) + " / Total: " + std::to_string(totalCount));
}
// ====================================================================
// UI描画専用の関数
// ====================================================================
void GamePlayScene::DrawUI() {
    bool isCinematic = IsCinematicMode();
    bool isGameOver = (player_ && player_->GetHp() <= 0.0f);

    // --- 4. 2D描画 (UIスプライト) ---
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());

    // ヘルパー: システムUIかどうかの判定
    auto IsPauseUI = [&](Sprite* sp) {
        return sp == poseBackSprite_ || sp == poseTextSprite_ ||
               sp == restartPoseTextSprite_ || sp == titleTextPoseSprite_ ||
               sp == optionPoseTextSprite_ || sp == optionControlsSprite_;
    };
    auto IsGameOverUI = [&](Sprite* sp) {
        return sp == gameOverTextSprite_ || sp == restartTextSprite_ ||
               sp == titleTextSprite_;
    };

    // スプライト一括描画の制御
    for (auto& sprite : sprites_) {
        Sprite* sp = sprite.get();
        if (!sp) continue;

        bool isPause = IsPauseUI(sp);
        bool isGameOverUI = IsGameOverUI(sp);
        bool isOption = optionUI_.IsOptionSprite(sp);

        if (isOptionMenu_) {
            // オプション中はオプション関連かつ現在のタブに該当するもののみ
            if (isOption && optionUI_.IsSpriteVisibleInCurrentTab(sp)) {
                sp->Draw();
            }
        }
        else if (isPaused_) {
            // ポーズ中はポーズ関連のみ
            if (isPause) sp->Draw();
        }
        else if (isGameOver) {
            // ゲームオーバー中はゲームオーバー関連のみ
            if (isGameOverUI) sp->Draw();
        }
        else if (!isCinematic) {
            // 通常時（シネマティックでない時）はゲーム用UI（システム系以外）を表示
            if (!isPause && !isGameOverUI && !isOption) {
                sp->Draw();
            }
        }
    }

    if (isGameOver && !isPaused_ && !isOptionMenu_) {
        DrawGameOverTitleGlyphs();
        if (gameOverEnterTextSprite_) {
            gameOverEnterTextSprite_->Draw();
        }
    }

    // ロックオンアイコンとタイムアタックUI
    if (isDrawLockOn_ && lockOnSprite_ && !isPaused_ && !isOptionMenu_ && !isCinematic && !isGameOver) {
        lockOnSprite_->Draw();
    }
    if (timeAttackUI_ && hasBossAppeared_ && !isPaused_ && !isOptionMenu_ && !isCinematic && !isGameOver) {
        timeAttackUI_->Draw();
    }

    // オプションUI固有の描画（動的生成アイコンなど）
    if (isOptionMenu_) {
        optionUI_.DrawKeyIcons();
    }
}

void GamePlayScene::DrawShadow() {
    if (objectManager_) {

        objectManager_->DrawShadow();
    }
}

bool GamePlayScene::IsCinematicMode() const {
    bool isBossDying = boss_ && boss_->IsDyingSequence();
    bool isBossHpHalf = boss_ && boss_->IsHpHalfEventActive();
    return (movieState_ != MovieState::kNone) || isBossMoviePlaying_ || isBossDying || isBossHpHalf;
}

void GamePlayScene::UpdateUI(float deltaTime) {
    // 1. プレイヤーのHP同期
    if (player_ && playerHpBarSprite_) {
        float currentHp = player_->GetHp();
        float maxHp = player_->GetMaxHp();
        float hpRatio = Math::Clamp(currentHp / maxHp, 0.0f, 1.0f);

        // ダメージを受けた瞬間を検知してタイマーをセット
        if (hpRatio < playerPrevHpRatio_) {
            playerDamageDelayTimer_ = 0.6f; // 0.6秒待機してから減り始める
        }
        playerPrevHpRatio_ = hpRatio;

        // 緑色のメインバーは即座に反映
        playerHpBarSprite_->SetSize({ playerHpBarMaxWidth_ * hpRatio, playerHpBarSprite_->GetSize().y });

        // 赤色のダメージバー演出 (現在のHPを追いかける)
        if (playerVisualHp_ > hpRatio) {
            if (playerDamageDelayTimer_ > 0.0f) {
                // 待機中
                playerDamageDelayTimer_ -= deltaTime;
            } else {
                // 待機終了：徐々に減らす
                playerVisualHp_ -= 0.15f * deltaTime; // 秒間15%減少 (よりゆっくりに)
                if (playerVisualHp_ < hpRatio) playerVisualHp_ = hpRatio;
            }
        } else {
            // 回復した、または初期化：即座に追いつく
            playerVisualHp_ = hpRatio;
            playerDamageDelayTimer_ = 0.0f;
        }

        if (playerDamageBarSprite_) {
            playerDamageBarSprite_->SetSize({ playerHpBarMaxWidth_ * playerVisualHp_, playerDamageBarSprite_->GetSize().y });
        }
    }

    // 1.5 プレイヤーの回避クールタイム同期
    if (player_ && playerDashBarSprite_ && playerDashBackSprite_) {
        float dashRatio = player_->GetDashCooldownRatio();
        
        // ゲージの長さを反映
        playerDashBarSprite_->SetSize({ playerDashBarMaxWidth_ * dashRatio, playerDashBarSprite_->GetSize().y });
        
        // 状態によって色を変える
        if (dashRatio >= 1.0f) {
            // 準備完了：明るいシアン
            playerDashBarSprite_->SetColor({ 0.0f, 1.0f, 1.0f, 1.0f });
        } else {
            // クールダウン中：オレンジ
            playerDashBarSprite_->SetColor({ 1.0f, 0.6f, 0.0f, 1.0f });
        }
    }

    if (boss_) {
        // =======================================================
        // ボスUIの表示・非表示制御
        // ムービーが終了（!isBossMoviePlaying_）したら表示する
        // =======================================================
        float alpha = (hasBossAppeared_ && !isBossMoviePlaying_) ? 1.0f : 0.0f;

        auto SetAlpha = [](Sprite* s, float a) {
            if (s) {
                Vector4 c = s->GetColor();
                c.w = a;
                s->SetColor(c);
            }
            };

        SetAlpha(bossHpBarSprite_, alpha);
        SetAlpha(bossDamageBarSprite_, alpha); // ダメージバーも同期
        SetAlpha(barrierHpBarSprite_, alpha);
        SetAlpha(barrierDamageBarSprite_, alpha);
        SetAlpha(bossHpBackSprite_, alpha);
        SetAlpha(bossNameSprite_, alpha);
        SetAlpha(bossIconSprite_, alpha);
        SetAlpha(shieldIconSprite_, alpha);
        SetAlpha(bossHpFrameSprite_, alpha);
        SetAlpha(bariaFrameSprite_, alpha);
        SetAlpha(hpFrameSprite_, alpha);

        // --- A. メインHPバーの同期 ---
        if (bossHpBarSprite_) {
            float hpRatio = Math::Clamp(boss_->GetHp() / boss_->GetMaxHp(), 0.0f, 1.0f);
            
            // ダメージを受けた瞬間を検知
            if (hpRatio < bossPrevHpRatio_) {
                bossDamageDelayTimer_ = 0.8f; // ボスはより長く待機 (0.8秒)
                // ボスアイコンシェイクを設定
                bossIconShakeTimer_ = 0.3f; // 0.3秒シェイク
                bossIconShakeIntensity_ = 8.0f; // シェイクの強さ
            }
            bossPrevHpRatio_ = hpRatio;

            // 赤色のメインバーは即座に反映
            bossHpBarSprite_->SetSize({ bossHpBarMaxWidth_ * hpRatio, bossHpBarSprite_->GetSize().y });

            // 白色のダメージバー演出
            if (bossVisualHp_ > hpRatio) {
                if (bossDamageDelayTimer_ > 0.0f) {
                    bossDamageDelayTimer_ -= deltaTime;
                } else {
                    bossVisualHp_ -= 0.1f * deltaTime; // 秒間10%減少 (ボスは重厚感を出すためにさらにゆっくり)
                    if (bossVisualHp_ < hpRatio) bossVisualHp_ = hpRatio;
                }
            } else {
                bossVisualHp_ = hpRatio;
                bossDamageDelayTimer_ = 0.0f;
            }

            if (bossDamageBarSprite_) {
                bossDamageBarSprite_->SetSize({ bossHpBarMaxWidth_ * bossVisualHp_, bossDamageBarSprite_->GetSize().y });
            }
        }

        // --- B. バリアHPバーの同期 ---
        if (barrierHpBarSprite_) {
            float bRatio = Math::Clamp(
                boss_->GetBarrierHp() / boss_->GetMaxBarrierHp(), 0.0f, 1.0f);

            // スタン中かどうかの判定
            bool isBossStunned = (boss_->GetState() == BossCore::State::Weak);

            if (isBossStunned) {
                // スタン（ダウン）中は、スタンゲージの回復（0 -> 100%）を直接表示する
                barrierVisualMain_ = bRatio;
                barrierVisualDamage_ = bRatio;
                barrierPrevHpRatio_ = bRatio;
            }
            else {
                // 1. ダメージを受けた瞬間を検知
                if (bRatio < barrierPrevHpRatio_) {
                    barrierDamageDelayTimer_ = 0.5f;
                    // バリアアイコンシェイクを設定
                    shieldIconShakeTimer_ = 0.3f; // 0.3秒シェイク
                    shieldIconShakeIntensity_ = 8.0f; // シェイクの強さ
                }
                barrierPrevHpRatio_ = bRatio;

                // 2. メインバー (Cyan) の更新
                if (barrierVisualMain_ < bRatio) {
                    // 復帰時: 徐々に増やす (Kirby-style 0->100 recovery)
                    barrierVisualMain_ += 0.4f * deltaTime; // 秒間40%回復
                    if (barrierVisualMain_ > bRatio) barrierVisualMain_ = bRatio;
                }
                else {
                    // 通常時・ダメージ時: 即座に反映
                    barrierVisualMain_ = bRatio;
                }

                // 3. ダメージバー (White) の更新
                if (barrierVisualDamage_ > bRatio) {
                    // ダメージ中: 待機後に徐々に減らす
                    if (barrierDamageDelayTimer_ > 0.0f) {
                        barrierDamageDelayTimer_ -= deltaTime;
                    }
                    else {
                        barrierVisualDamage_ -= 0.15f * deltaTime; // 秒間15%減少
                        if (barrierVisualDamage_ < bRatio) barrierVisualDamage_ = bRatio;
                    }
                }
                else {
                    // 回復中または安定: メインバーに同期
                    barrierVisualDamage_ = barrierVisualMain_;
                }
            }

            // スプライトに反映
            barrierHpBarSprite_->SetSize(
                { barrierHpBarMaxWidth_ * barrierVisualMain_, barrierHpBarSprite_->GetSize().y });

            if (barrierDamageBarSprite_) {
                barrierDamageBarSprite_->SetSize(
                    { barrierHpBarMaxWidth_ * barrierVisualDamage_, barrierDamageBarSprite_->GetSize().y });
            }
        }

        // --- C. アイコンのシェイク更新 ---
        if (bossIconSprite_) {
            if (bossIconShakeTimer_ > 0.0f) {
                bossIconShakeTimer_ -= deltaTime;
                float offsetX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * bossIconShakeIntensity_;
                float offsetY = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * bossIconShakeIntensity_;
                bossIconSprite_->SetPosition({ bossIconBasePos_.x + offsetX, bossIconBasePos_.y + offsetY });
                if (bossIconShakeTimer_ <= 0.0f) {
                    bossIconSprite_->SetPosition(bossIconBasePos_);
                }
            }
        }

        if (shieldIconSprite_) {
            if (shieldIconShakeTimer_ > 0.0f) {
                shieldIconShakeTimer_ -= deltaTime;
                float offsetX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shieldIconShakeIntensity_;
                float offsetY = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shieldIconShakeIntensity_;
                shieldIconSprite_->SetPosition({ shieldIconBasePos_.x + offsetX, shieldIconBasePos_.y + offsetY });
                if (shieldIconShakeTimer_ <= 0.0f) {
                    shieldIconSprite_->SetPosition(shieldIconBasePos_);
                }
            }
        }
    }
    // --- ミッションテキストのアニメーション演出 ---
    auto SetAlpha = [](Sprite* s, float a) {
        if (s) {
            Vector4 c = s->GetColor();
            c.w = a;
            s->SetColor(c);
        }
        };

    // 1. missionText_mission & missionText_line (既存の処理を尊重)
    if (hasBossAppeared_) {
        SetAlpha(missionText_mission_, 0.0f);
        SetAlpha(missionText_line_, 1.0f); // ボス戦中もラインは維持
    }
    else if (hasBridgeDropped_) {
        SetAlpha(missionText_mission_, 0.0f);
        SetAlpha(missionText_line_, 1.0f);
    }
    else if (missionInitialShown_) {
        SetAlpha(missionText_mission_, 1.0f);
        SetAlpha(missionText_line_, 1.0f);
    }

    // --- タイマー更新 ---
    if (missionSwitchDelayTimer_ > 0.0f) {
        missionSwitchDelayTimer_ -= deltaTime;
    }

    // 2. missionText_Mark (回転しながら出現)
    if (missionInitialShown_ && missionText_Mark_) {
        if (missionSwitchDelayTimer_ > 0.0f) return; // 待機中
        missionMarkAnimProgress_ = std::min(1.0f, missionMarkAnimProgress_ + deltaTime * 2.0f);
        float scale = missionMarkAnimProgress_;
        float rot = (1.0f - missionMarkAnimProgress_) * 3.14159265f * 2.0f;
        missionText_Mark_->SetSize({ missionMarkBaseSize_.x * scale, missionMarkBaseSize_.y * scale });
        missionText_Mark_->SetRotation(rot);
        SetAlpha(missionText_Mark_, 1.0f);
    }

    // 3. lever -> go -> boss 遷移演出
    // A. Lever (初期ミッション)
    if (missionInitialShown_ && !isLeverOut_) {
        if (!hasTutorialMovieFinished_) {
            if (missionSwitchDelayTimer_ > 0.0f) return; // 待機中

            // 出現アニメーション（または表示維持）
            missionLeverAnimProgress_ = std::min(1.0f, missionLeverAnimProgress_ + deltaTime * 2.0f);
            if (missionText_lever_) {
                float offsetY = (1.0f - missionLeverAnimProgress_) * 20.0f;
                missionText_lever_->SetPosition({ missionLeverBasePos_.x, missionLeverBasePos_.y + offsetY });
                missionText_lever_->SetSize(missionLeverBaseSize_);
                SetAlpha(missionText_lever_, missionLeverAnimProgress_);
            }
            if (missionLeverAnimProgress_ >= 1.0f && !missionLeverSePlayed_) {
                missionLeverSePlayed_ = true;
                AudioPlayer::GetInstance()->PlaySE(seMissionHandle_, false, SaveDataManager::GetInstance()->GetSEVolume());
            }
        }
        else {
            if (missionSwitchDelayTimer_ > 0.0f) return; // 0.2秒待機

            // 完了アニメーション (Yスケール -> 0)
            leverOutProgress_ = std::min(1.0f, leverOutProgress_ + deltaTime * 4.0f);
            if (missionText_lever_) {
                missionText_lever_->SetSize({ missionLeverBaseSize_.x, missionLeverBaseSize_.y * (1.0f - leverOutProgress_) });
            }
            if (leverOutProgress_ >= 1.0f) {
                isLeverOut_ = true;
                SetAlpha(missionText_lever_, 0.0f);
            }
        }
    }

    // B. Go (レバー完了後)
    if (isLeverOut_ && !isGoOut_) {
        if (!hasBossAppeared_ || isBossMoviePlaying_) { // ムービー中も維持するように変更
            // 出現アニメーション
            missionGoAnimProgress_ = std::min(1.0f, missionGoAnimProgress_ + deltaTime * 2.0f);
            if (missionText_go_) {
                float offsetY = (1.0f - missionGoAnimProgress_) * 20.0f;
                missionText_go_->SetPosition({ missionGoBasePos_.x, missionGoBasePos_.y + offsetY });
                missionText_go_->SetSize(missionGoBaseSize_);
                SetAlpha(missionText_go_, missionGoAnimProgress_);
            }
            if (missionGoAnimProgress_ >= 1.0f && !missionGoSePlayed_) {
                missionGoSePlayed_ = true;
                AudioPlayer::GetInstance()->PlaySE(seMissionHandle_, false, SaveDataManager::GetInstance()->GetSEVolume());
            }
        }
        else {
            if (missionSwitchDelayTimer_ > 0.0f) return; // 0.2秒待機

            // 完了アニメーション (Yスケール -> 0)
            goOutProgress_ = std::min(1.0f, goOutProgress_ + deltaTime * 4.0f);
            if (missionText_go_) {
                missionText_go_->SetSize({ missionGoBaseSize_.x, missionGoBaseSize_.y * (1.0f - goOutProgress_) });
            }
            if (goOutProgress_ >= 1.0f) {
                isGoOut_ = true;
                SetAlpha(missionText_go_, 0.0f);
            }
        }
    }

    // C. Boss (ボス戦中)
    if (isGoOut_) {
        missionBossAnimProgress_ = std::min(1.0f, missionBossAnimProgress_ + deltaTime * 2.0f);
        if (missionText_boss_) {
            float offsetY = (1.0f - missionBossAnimProgress_) * 20.0f;
            missionText_boss_->SetPosition({ missionBossBasePos_.x, missionBossBasePos_.y + offsetY });
            missionText_boss_->SetSize(missionBossBaseSize_);
            SetAlpha(missionText_boss_, missionBossAnimProgress_);
        }
        if (missionBossAnimProgress_ >= 1.0f && !missionBossSePlayed_) {
            missionBossSePlayed_ = true;
            AudioPlayer::GetInstance()->PlaySE(seMissionHandle_, false, SaveDataManager::GetInstance()->GetSEVolume());
        }
    }
}

void GamePlayScene::PlayBridgeMagmaSeIfNeeded() {
    if (!audioPlayer_ || isBridgeMagmaSePlaying_ ||
        seBridgeMagmaHandle_ == AudioPlayer::kInvalidAudioHandle) {
        return;
    }

    audioPlayer_->PlaySE(
        seBridgeMagmaHandle_,
        true,
        SaveDataManager::GetInstance()->GetSEVolume() * 0.85f);
    isBridgeMagmaSePlaying_ = true;
}

void GamePlayScene::StopBridgeMagmaSe() {
    if (!audioPlayer_) {
        isBridgeMagmaSePlaying_ = false;
        return;
    }

    if (isBridgeMagmaSePlaying_ ||
        audioPlayer_->IsPlaying(seBridgeMagmaHandle_)) {
        audioPlayer_->StopSe(seBridgeMagmaHandle_);
    }
    isBridgeMagmaSePlaying_ = false;
}

void GamePlayScene::StartBridgeDropMovie() {
    if (movieState_ != MovieState::kNone || hasBridgeDropped_)
        return;

    movieState_ = MovieState::kBridgeDrop;
    movieTimer_ = 0.0f;
    hasBridgeDropped_ = true;
    bridgeCenterMagmaImpactPlayed_ = false;
    bridgeBackMagmaImpactPlayed_ = false;
    bridgeFrontMagmaImpactPlayed_ = false;
    StopBridgeMagmaSe();
    bridgeFallSe1Played_ = false;
    bridgeFallSe2Played_ = false;
    bridgeFallSe3Played_ = false;

    // CinematicCamera を探してムービーを再生する
    for (auto& obj : objectManager_->GetObjects()) {
        if (obj->GetName() == "Cinematic_Camera_Bridge") {
            if (obj->recorder_) {
                // Play(fileName, loop, isRelative, isCinematic)
                obj->recorder_->Play("bridge_movie", false, false, true);
            }
            break;
        }
    }
}

// ========================================================
// ボス登場ムービーの開始処理
// ========================================================
void GamePlayScene::StartBossAppearanceMovie() {
    if (isBossMoviePlaying_ || !boss_ || hasBossAppeared_)
        return;

    isBossMoviePlaying_ = true;
    hasBossAppeared_ = true;
    audioPlayer_->StopBGM(); // ボス登場演出中はBGMを止める
    audioPlayer_->PlaySE(seMissionClear3Handle_, false, SaveDataManager::GetInstance()->GetSEVolume());
    movieTimer_ = 0.0f;

    // ボスコンテナのパーティクルを停止
    GPUParticleManager::GetInstance()->StopAutoEmitter(
        bossContainerTopParticleId_);
    GPUParticleManager::GetInstance()->StopAutoEmitter(
        bossContainerBottomParticleId_);
    bossContainerTopParticleId_ = 0;
    bossContainerBottomParticleId_ = 0;

    // プレイヤーを固定
    if (player_) {
        movieStoredPlayerPos_ = player_->GetWorldPosition();
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
        player_->SetIsControlActive(false);
        player_->SetIsPhysicsActive(false);
    }

    // ====================================================
    // a.json（カメラのアニメーション）を再生する
    // ====================================================
    for (auto& obj : objectManager_->GetObjects()) {
        if (obj->GetName() ==
            "Cinematic_Camera_Boss") { // ボス用のシネマティックカメラオブジェクトを用意しておく
            if (obj->recorder_) {
                // "a" という名前のJSONを再生
                obj->recorder_->Play("a", false, false, true);
            }
            break;
        }
    }

    // ボス側にはカメラ移動以外の演出（ブロックが集まる等）だけをやらせる
    boss_->StartAppearance();
}

#ifdef USE_IMGUI
bool GamePlayScene::PrepareBridgeDropPreviewForDebug() {
    movieState_ = MovieState::kNone;
    movieTimer_ = 0.0f;
    hasBridgeDropped_ = false;
    bridgeCenterMagmaImpactPlayed_ = false;
    bridgeBackMagmaImpactPlayed_ = false;
    bridgeFrontMagmaImpactPlayed_ = false;
    StopBridgeMagmaSe();
    bridgeFallSe1Played_ = false;
    bridgeFallSe2Played_ = false;
    bridgeFallSe3Played_ = false;
    GameProgress::GetInstance()->hasBridgeDropped = false;

    bool hasBridgeBlock = false;
    for (auto& obj : objectManager_->GetObjects()) {
        if (!obj) {
            continue;
        }

        const std::string& name = obj->GetName();
        if (name == "Bridge_Block_Front" || name == "Bridge_Block_Center" || name == "Bridge_Block_Back") {
            Transform* trans = obj->GetTransform();
            if (name == "Bridge_Block_Front") {
                trans->translate = { 0.0f, 3.0f, -88.0f };
            } else if (name == "Bridge_Block_Center") {
                trans->translate = { 0.0f, 9.7749996f, -115.1679993f };
            } else {
                trans->translate = { 0.0f, 16.5489998f, -142.3359985f };
            }

            trans->rotate = { 0.2443461f, 0.0f, 0.0f };
            trans->quaternion = Math::EulerToQuaternion(trans->rotate);
            trans->isQuaternionMaster = true;
            obj->isDead = false;
            obj->SetIsVisible(true);
            obj->SetCollisionAttribute(kGround);
            obj->UpdateLocalMatrix();
            obj->UpdateWorldMatrix();
            hasBridgeBlock = true;
            continue;
        }

        if (name.find("Bridge_Collision") != std::string::npos) {
            obj->isDead = false;
            obj->SetCollisionAttribute(kGround);
        } else if (name.find("Battle_Field_Collision_Box_South") != std::string::npos) {
            obj->SetCollisionAttribute(0);
        }
    }

    if (player_) {
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
        player_->SetIsControlActive(true);
        player_->SetIsPhysicsActive(true);
    }

    return hasBridgeBlock;
}
#endif

void GamePlayScene::DrawImGui() {
#ifdef USE_IMGUI
    // Begin/End を削除し、既存の Inspector ウィンドウ内に描画されるようにする
    if (ImGui::CollapsingHeader("Game Debug Controls", ImGuiTreeNodeFlags_DefaultOpen)) {

        if (ImGui::TreeNode("演出確認 (Cinematic Preview)")) {
            if (movieState_ == MovieState::kBridgeDrop) {
                ImGui::Text("橋落下演出を再生中");
            } else if (ImGui::Button("橋落下演出を再生", ImVec2(-1, 30))) {
                if (PrepareBridgeDropPreviewForDebug()) {
                    StartBridgeDropMovie();
                    isBridgeDropPreviewForDebug_ = (movieState_ == MovieState::kBridgeDrop);
                    DebugConsole::GetInstance()->AddLog("【DEBUG】 橋落下演出を手動再生しました");
                } else {
                    DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "【DEBUG】 橋ブロックが見つからないため、橋落下演出を再生できませんでした");
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("プレイヤー・ボス状態 (Player/Enemy Status)")) {
            if (ImGui::Button("プレイヤー HP -> 0")) {
                if (player_ && player_->param_.has_value()) {
                    player_->param_->hp = 0.0f;
                }
            }

            ImGui::Separator();

            if (ImGui::Button("ボス HP -> 51% (半減演出の直前)")) {
                if (boss_ && boss_->param_.has_value()) {
                    boss_->param_->hp = boss_->param_->maxHp * 0.51f;
                }
            }

            if (ImGui::Button("ボス HP -> 25%")) {
                if (boss_ && boss_->param_.has_value()) {
                    boss_->param_->hp = boss_->param_->maxHp * 0.25f;
                }
            }

            if (ImGui::Button("ボス HP -> 0% (強制爆散)")) {
                if (boss_) {
                    if (boss_->param_.has_value()) {
                        boss_->param_->hp = 0.0f;
                    }
                    boss_->StartDeathSequence();
                }
            }

            ImGui::Separator();

            if (ImGui::Button("ボス スタンゲージ -> 25%")) {
                if (boss_) {
                    boss_->SetBarrierHp(boss_->GetMaxBarrierHp() * 0.25f);
                }
            }

            if (ImGui::Button("ボス スタンゲージ -> 0% (強制ダウン)")) {
                if (boss_) {
                    // 現在のバリアHP分ダメージを与えて強制的にスタン演出をトリガーする
                    boss_->TakeBarrierDamage(boss_->GetBarrierHp(), nullptr);
                }
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("プレイヤーの攻撃力設定 (Player Attack Balance)")) {
            if (player_) {
                PlayerAttackParams& params = player_->GetAttackParams();
                ImGui::DragFloat("コンボ1 ダメージ", &params.damageCombo1, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("コンボ2 ダメージ", &params.damageCombo2, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("コンボ3 ダメージ", &params.damageCombo3, 0.1f, 0.0f, 200.0f);
                ImGui::DragFloat("落下攻撃 ダメージ", &params.damagePlunge, 0.1f, 0.0f, 100.0f);

                ImGui::Spacing();
                if (ImGui::Button("プレイヤーパラメータを保存")) {
                    player_->SaveAttackParams();
                    DebugConsole::GetInstance()->AddLog("【システム】 プレイヤーの攻撃パラメータをJSONに保存しました。");
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("ボス・エネミーの攻撃力設定 (Boss Attack Balance)")) {
            if (boss_) {
                BossAttackParams& params = boss_->GetAttackParams();
                ImGui::DragFloat("突進攻撃力 (技1)", &params.damageRush, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("弾幕攻撃力 (技2)", &params.damageShoot, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("ハンマー攻撃力 (技3)", &params.damageHammer, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("壁攻撃 (技4)", &params.damageWall, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("人型叩きつけ攻撃力 (技5)", &params.damageHumanoid, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("極太レーザー攻撃力 (技6)", &params.damageLaser, 0.5f, 0.0f, 250.0f);
                ImGui::DragFloat("ブロック吸収 (技7)", &params.damageAbsorb, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("最終奥義メテオ攻撃力 (技8)", &params.damageFinal, 0.5f, 0.0f, 250.0f);
                ImGui::DragFloat("ファンネルレーザー攻撃力 (技9)", &params.damageFunnels, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("スライム接触ダメージ", &params.damageSlime, 0.5f, 0.0f, 100.0f);
                ImGui::DragFloat("ボム爆発ダメージ", &params.damageBomb, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("ボム跳ね返しボスダメージ", &params.damageBombReflect, 0.5f, 0.0f, 150.0f);

                ImGui::Separator();
                ImGui::Text("=== ボス攻撃パターン・確率設定 ===");

                // 攻撃名の定義
                const char* attackNames[] = {
                    "なし (None)",
                    "突進 (技1) [Rush]",
                    "弾幕 (技2) [Shoot]",
                    "ハンマー (技3) [Hammer]",
                    "壁攻撃 (技4) [Wall]",
                    "人型攻撃 (技5) [Humanoid]",
                    "極太レーザー (技6) [Laser]",
                    "ブロック吸収 (技7) [Absorb]",
                    "最終奥義 (技8) [Final]",
                    "ファンネル (技9) [Funnels]",
                    "スポーン (技10) [Spawn]"
                };
                int attackIds[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

                auto DrawAttackList = [&](const char* label, std::vector<AttackWeight>& attackList) {
                    if (ImGui::TreeNode(label)) {
                        for (size_t i = 0; i < attackList.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i));

                            // 選択中のインデックスを特定
                            int currentAttackIdx = 0;
                            for (int idx = 0; idx < 11; ++idx) {
                                if (attackIds[idx] == attackList[i].id) {
                                    currentAttackIdx = idx;
                                    break;
                                }
                            }

                            ImGui::SetNextItemWidth(200.0f);
                            if (ImGui::Combo("攻撃種類", &currentAttackIdx, attackNames, IM_ARRAYSIZE(attackNames))) {
                                attackList[i].id = attackIds[currentAttackIdx];
                            }

                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(100.0f);
                            ImGui::DragInt("確率", &attackList[i].weight, 1.0f, 0, 1000);

                            ImGui::SameLine();
                            if (ImGui::Button("❌ 削除")) {
                                attackList.erase(attackList.begin() + i);
                                ImGui::PopID();
                                break;
                            }

                            ImGui::PopID();
                        }

                        if (ImGui::Button("➕ 攻撃パターンを追加 (ADD)")) {
                            attackList.push_back({ 1, 30 }); // 突進・重み30をデフォルト設定
                        }

                        ImGui::TreePop();
                    }
                };

                DrawAttackList("第一形態の攻撃確率 (HP > 50%)", params.phase1Attacks);
                DrawAttackList("第二形態の攻撃確率 (HP <= 50%)", params.phase2Attacks);

                ImGui::Separator();
                ImGui::DragFloat("ボス 最大バリアHP", &params.maxBarrierHp, 1.0f, 10.0f, 1000.0f);
                ImGui::DragFloat("装甲ブロック単体のHP", &params.maxArmorBlockHp, 1.0f, 10.0f, 1000.0f);
                ImGui::DragFloat("ボス スタン時間 (秒)", &params.stunDuration, 0.1f, 1.0f, 60.0f);
                ImGui::DragFloat("突進クラッシュスタン時間 (秒)", &params.crashStunDuration, 0.1f, 0.1f, 30.0f);
                ImGui::DragInt("低装甲時 吸収トリガー数", &params.lowArmorThreshold, 0.1f, 0, 10);
                ImGui::SliderInt("低装甲時 吸収発動確率 (％)", &params.lowArmorAbsorbRate, 0, 100);

                if (ImGui::Button("ボスバリア・全装甲を全回復")) {
                    boss_->FullyRecoverBarrierAndArmor();
                    DebugConsole::GetInstance()->AddLog("【システム】 ボスのバリアと全装甲HPを全回復しました。");
                }

                ImGui::Spacing();
                if (ImGui::Button("ボスパラメータを保存")) {
                    boss_->SaveAttackParams();
                    DebugConsole::GetInstance()->AddLog("【システム】 ボスの攻撃パラメータおよび確率設定をJSONに保存しました。");
                }
            }
            ImGui::TreePop();
        }

        ImGui::Separator();

        if (ImGui::Button("Skip Tutorial", ImVec2(-1, 30))) {
            if (player_) {
                // 1. 速度と座標のリセット
                player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
                player_->GetTransform()->translate = { 0.0f, 1.3f, -68.0f };
                player_->UpdateLocalMatrix();
                player_->UpdateWorldMatrix();

                // 2. カメラを即座にワープ地点へ同期（ラグを防ぐ）
                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) {
                    camera->SetTarget(player_->GetWorldPosition());
                    camera->Update();
                }

                // 3. 各種フラグを「完了」にセット
                hasFinishedTutorial_ = true;
                hasTutorialMovieFinished_ = true;
                hasBridgeDropped_ = true; // 橋の状態も同期
                GameProgress::GetInstance()->hasFinishedTutorial = true;
                doorOpenProgress_ = 1.0f;
                missionInitialShown_ = true;
                missionGoShown_ = true;

                // 4. チュートリアル関連のオブジェクト削除 ＆ ボスエリアの床を有効化
                for (auto& obj : objectManager_->GetObjects()) {
                    std::string name = obj->GetName();

                    // チュートリアル関係は消す
                    if (name.find("Bridge_") != std::string::npos || name.find("Tutorial_") != std::string::npos) {
                        obj->SetIsVisible(false);
                        obj->isDead = true;
                    }
                    // ボスエリアの地面の当たり判定をONにする
                    else if (name.find("Battle_Field_Collision_Box_") != std::string::npos) {
                        obj->SetCollisionAttribute(kGround);
                    }
                }
                DebugConsole::GetInstance()->AddLog("【DEBUG】 チュートリアルをスキップしました");
            }
        }
    }
#endif
}

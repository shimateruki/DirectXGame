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
#include "Fade.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kTextFile1 = "Resources/sprite/generated/text/text_1_3c19efbf.png";
constexpr const char* kTextFile2 = "Resources/sprite/generated/text/text_2_3d19f152.png";
constexpr const char* kTextFile3 = "Resources/sprite/generated/text/text_3_3e19f2e5.png";
constexpr const char* kTextStartFromBeginning = "Resources/sprite/generated/text/text_text_c8a6dc24.png";
constexpr const char* kTextContinue = "Resources/sprite/generated/text/text_text_46356cfa.png";
constexpr const char* kTextStartQuestion = "Resources/sprite/generated/text/text_text_0bbc5855.png";
constexpr const char* kTextDeleteFile = "Resources/sprite/generated/text/text_text_1f05b6eb.png";
constexpr const char* kTextDeleteQuestion = "Resources/sprite/generated/text/text_text_8aae87df.png";
constexpr const char* kTextYes = "Resources/sprite/generated/text/text_text_a214f5c6.png";
constexpr const char* kTextBack = "Resources/sprite/generated/text/text_text_70188c3d.png";
constexpr const char* kTextPlayTime = "Resources/sprite/generated/text/text_text_46271c76.png";
constexpr const char* kCrownIcon = "Resources/sprite/ui/title/crown_progress_icon.png";
constexpr const char* kStarIcon = "Resources/sprite/ui/hud/stage_star_filled.png";
constexpr const char* kSlimeIcon = "Resources/sprite/title/slime_save_icon.png";
constexpr const char* kXIcon = "Resources/sprite/ui/hud/xUi.png";
constexpr const char* kWhite = "Resources/sprite/common/white.png";
constexpr const char* kSaveSlotCard = "ui/title/save_slot_card.png";
constexpr const char* kSaveSlotCardSelected = "ui/title/save_slot_card_selected.png";
constexpr const char* kSaveSlotCardEmpty = "ui/title/save_slot_card_empty.png";
constexpr const char* kSaveDeleteButton = "ui/title/save_delete_button.png";
constexpr const char* kSaveDeleteButtonSelected = "ui/title/save_delete_button_selected.png";
constexpr const char* kTitleHeroSlimeName = "TitleHeroSlime";
constexpr float kTitleIntroLastDelay = 0.385f;
constexpr float kTitleIntroGlyphDuration = 0.45f;
constexpr float kTitleIntroMenuDelay = kTitleIntroLastDelay + kTitleIntroGlyphDuration + 0.20f;
constexpr float kTitleIntroMenuFadeDuration = 0.35f;
constexpr float kTitleIntroPi = 3.1415926535f;
const Vector2 kMenuStartFramePosition = { 1536.0f, 712.0f };
const Vector2 kMenuSettingFramePosition = { 1536.0f, 818.0f };
const Vector2 kMenuStartTextureLeftTop = { 2.0f, 2.0f };
const Vector2 kMenuStartTextureSize = { 278.0f, 42.0f };
const Vector2 kMenuSettingTextureLeftTop = { 5.0f, 3.0f };
const Vector2 kMenuSettingTextureSize = { 75.0f, 37.0f };
const Vector2 kMenuCursorTextureLeftTop = { 115.0f, 218.0f };
const Vector2 kMenuCursorTextureSize = { 1024.0f, 827.0f };
const Vector3 kTitleHeroDefaultPosition = { -2.2f, -1.95f, 4.8f };
const Vector3 kTitleHeroDefaultScale = { 3.0f, 3.0f, 3.0f };

const std::array<const char*, 3> kFileTextPaths = {
    kTextFile1,
    kTextFile2,
    kTextFile3
};

struct TitleLogoGlyphLayout {
    const char* name;
    const char* texture;
    Vector2 position;
    Vector2 size;
    float delay;
};

const std::array<TitleLogoGlyphLayout, 8> kTitleLogoGlyphLayouts = {
    TitleLogoGlyphLayout{ "titleLogoChar_0", "title/logo/slime_adventure_char_00_su.png", { 596.5f, 147.3f }, { 102.8f, 101.1f }, 0.000f },
    TitleLogoGlyphLayout{ "titleLogoChar_1", "title/logo/slime_adventure_char_01_ra.png", { 699.6f, 142.7f }, { 101.6f, 102.8f }, 0.055f },
    TitleLogoGlyphLayout{ "titleLogoChar_2", "title/logo/slime_adventure_char_02_i.png", { 801.8f, 146.3f }, { 103.7f, 104.1f }, 0.110f },
    TitleLogoGlyphLayout{ "titleLogoChar_3", "title/logo/slime_adventure_char_03_mu.png", { 902.3f, 147.7f }, { 108.3f, 102.8f }, 0.165f },
    TitleLogoGlyphLayout{ "titleLogoChar_4", "title/logo/slime_adventure_char_04_no.png", { 1018.5f, 139.1f }, { 107.4f, 101.6f }, 0.220f },
    TitleLogoGlyphLayout{ "titleLogoChar_5", "title/logo/slime_adventure_char_05_dai.png", { 1114.9f, 147.3f }, { 109.5f, 110.3f }, 0.275f },
    TitleLogoGlyphLayout{ "titleLogoChar_6", "title/logo/slime_adventure_char_06_bou.png", { 1221.7f, 150.6f }, { 104.9f, 109.5f }, 0.330f },
    TitleLogoGlyphLayout{ "titleLogoChar_7", "title/logo/slime_adventure_char_07_ken.png", { 1327.3f, 147.5f }, { 110.4f, 111.6f }, 0.385f }
};

float SmoothStep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool IsFadePlayingForSceneIntro() {
    const Fade::Status status = Fade::GetInstance()->GetStatus();
    return status == Fade::Status::FadeIn ||
        status == Fade::Status::FadeOut ||
        status == Fade::Status::IrisIn ||
        status == Fade::Status::IrisOut;
}

Object3d* FindObjectByName(BaseScene* scene, const std::string& name) {
    if (!scene) return nullptr;
    for (auto& object : scene->GetObjects()) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

}

void TitleScene::Initialize() {
    // --- 1. システム基盤の取得 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 2. モデルのプリロード ---
    ModelManager::GetInstance()->LoadModel("Characters/player");
    ModelManager::GetInstance()->LoadModel("Characters/slime");
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

    skyboxTextureHandle_ = TextureManager::GetInstance()->Load("Resources/output_skybox.dds");
    skybox_ = std::make_unique<Skybox>();
    skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);

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
    
    titleTextSprite_ = GetSpriteByName("titleText.png");
    startTextSprite_ = GetSpriteByName("gameStartText.png");
    settingTextSprite_ = GetSpriteByName("setting.png");
    titleHeroSlime_ = FindObjectByName(this, kTitleHeroSlimeName);
    if (!titleHeroSlime_) {
        auto hero = std::make_unique<Object3d>();
        hero->Initialize(object3dCommon_.get());
        hero->SetName(kTitleHeroSlimeName);
        hero->SetClassName("Model");
        hero->SetSaveCategory("Object");
        hero->SetModel("Characters/slime");
        hero->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        hero->SetCollisionAttribute(0);
        hero->SetCollisionMask(0);
        hero->SetTranslate(kTitleHeroDefaultPosition);
        hero->SetScale(kTitleHeroDefaultScale);
        hero->UpdateLocalMatrix();
        hero->UpdateWorldMatrix();
        titleHeroSlime_ = hero.get();
        objectManager_->AddObject(std::move(hero));
    }
    if (titleHeroSlime_) {
        titleHeroBasePosition_ = titleHeroSlime_->GetTranslate();
        titleHeroBaseScale_ = titleHeroSlime_->GetScale();
        titleHeroBaseRotation_ = titleHeroSlime_->GetRotation();
        titleHeroBaseCaptured_ = true;
    }
    titleIntroTime_ = 0.0f;
    titleIntroComplete_ = false;
    InitializeMainMenuUI();
    if (startTextSprite_) {
        startTextBaseSize_ = startTextSprite_->GetSize();
        startTextBasePosition_ = startTextSprite_->GetPosition();
    }
    if (settingTextSprite_) {
        settingTextBaseSize_ = settingTextSprite_->GetSize();
        settingTextBasePosition_ = settingTextSprite_->GetPosition();
    }
    InitializeTitleLogoUI();
    settingsOverlay_ = std::make_unique<SettingsMenuOverlay>();
    settingsOverlay_->Initialize(spriteCommon_.get());
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
    for (auto& frame : saveSlotFrames_) {
        frame.fill(nullptr);
    }
    saveSlotIcons_.fill(nullptr);
    saveSlotNumberSprites_.fill(nullptr);
    saveSlotFileNameTexts_.fill(nullptr);
    saveSlotStatusTexts_.fill(nullptr);
    saveSlotLifeIcons_.fill(nullptr);
    saveSlotLifeXIcons_.fill(nullptr);
    for (auto& digits : saveSlotLifeDigits_) {
        digits.fill(nullptr);
    }
    saveSlotCrownIcons_.fill(nullptr);
    saveSlotCrownXIcons_.fill(nullptr);
    for (auto& digits : saveSlotCrownDigits_) {
        digits.fill(nullptr);
    }
    saveSlotStarIcons_.fill(nullptr);
    saveSlotStarXIcons_.fill(nullptr);
    for (auto& digits : saveSlotStarDigits_) {
        digits.fill(nullptr);
    }
    saveSlotPlayTimeLabels_.fill(nullptr);
    for (auto& digits : saveSlotPlayTimeDigits_) {
        digits.fill(nullptr);
    }
    saveSelectHeader_ = nullptr;
    saveDeleteButtonBack_ = nullptr;
    saveDeleteButtonText_ = nullptr;
    savePromptBubble_ = nullptr;
    savePromptText_ = nullptr;
    saveDeleteQuestionText_ = nullptr;
    saveConfirmYesText_ = nullptr;
    saveConfirmBackText_ = nullptr;
    titleTextSprite_ = nullptr;
    startTextSprite_ = nullptr;
    settingTextSprite_ = nullptr;
    mainMenuStartFrameSprite_ = nullptr;
    mainMenuSettingFrameSprite_ = nullptr;
    mainMenuArrowSprite_ = nullptr;
    mainMenuCursorSprite_ = nullptr;
    mainMenuPromptSprite_ = nullptr;
    titleHeroSlime_ = nullptr;
    titleHeroBaseCaptured_ = false;
    for (auto& glyph : titleLogoGlyphs_) {
        glyph = {};
    }
    titleIntroTime_ = 0.0f;
    titleIntroComplete_ = false;
    settingsOverlay_.reset();
    sprites_.clear();
    skybox_.reset();
    skyboxTextureHandle_ = 0;
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void TitleScene::Update(float deltaTime) {
    titleUiTime_ += deltaTime;
    const float introDeltaTime = IsFadePlayingForSceneIntro() ? 0.0f : deltaTime;
    UpdateTitleLogoIntro(introDeltaTime);

    const bool isSettingsOpen = settingsOverlay_ && settingsOverlay_->IsActive();
    if (isSettingsOpen) {
        settingsOverlay_->Update(deltaTime);
    } else if (titleMode_ == TitleMode::MainMenu) {
        if (!titleIntroComplete_) {
            UpdateSaveSlotUI();
            LightEditor::GetInstance()->Update();
            CameraEditor::GetInstance()->Update(player_, false);
            CameraManager::GetInstance()->Update();
            objectManager_->Update(deltaTime);
            particleSystem_->Update(deltaTime);
            UpdateTitleHeroAnimation(deltaTime);
            for (auto& sprite : sprites_) {
                sprite->Update();
            }
            if (mainMenuStartFrameSprite_) mainMenuStartFrameSprite_->Update();
            if (mainMenuSettingFrameSprite_) mainMenuSettingFrameSprite_->Update();
            if (mainMenuArrowSprite_) mainMenuArrowSprite_->Update();
            if (mainMenuCursorSprite_) mainMenuCursorSprite_->Update();
            if (mainMenuPromptSprite_) mainMenuPromptSprite_->Update();
            for (auto& sprite : titleUiSprites_) {
                sprite->Update();
            }
            BulletManager::GetInstance()->Update(deltaTime);
            CollisionManager::GetInstance()->Update();
            return;
        }
        UpdateMainMenu();
    } else {
        UpdateSaveSelect();
    }

    UpdateSaveSlotUI();

    // 常に実行されるマネージャ更新
    LightEditor::GetInstance()->Update();
    CameraEditor::GetInstance()->Update(player_, false);
    CameraManager::GetInstance()->Update();

    // オブジェクト一括更新 (ObjectManagerに委譲)
    UpdateTitleHeroAnimation(deltaTime);
    objectManager_->Update(deltaTime);
    particleSystem_->Update(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }
    if (mainMenuStartFrameSprite_) mainMenuStartFrameSprite_->Update();
    if (mainMenuSettingFrameSprite_) mainMenuSettingFrameSprite_->Update();
    if (mainMenuArrowSprite_) mainMenuArrowSprite_->Update();
    if (mainMenuCursorSprite_) mainMenuCursorSprite_->Update();
    if (mainMenuPromptSprite_) mainMenuPromptSprite_->Update();
    for (auto& sprite : titleUiSprites_) {
        sprite->Update();
    }

    // 各種グローバル更新
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
}

void TitleScene::InitializeTitleLogoUI() {
    if (titleTextSprite_) {
        titleTextSprite_->SetVisible(false);
    }

    for (size_t i = 0; i < kTitleLogoGlyphLayouts.size(); ++i) {
        const auto& layout = kTitleLogoGlyphLayouts[i];
        Sprite* sprite = GetSpriteByName(layout.name);
        if (!sprite) {
            sprite = CreateUISprite("Resources/sprite/" + std::string(layout.texture), layout.position, layout.size, { 1.0f, 1.0f, 1.0f, 0.0f });
            sprite->SetName(layout.name);
            sprite->SetTextureName(layout.texture);
        }

        sprite->SetAnchorPoint({ 0.5f, 0.5f });
        sprite->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        sprite->SetVisible(false);

        titleLogoGlyphs_[i].sprite = sprite;
        titleLogoGlyphs_[i].basePosition = sprite->GetPosition();
        titleLogoGlyphs_[i].baseSize = sprite->GetSize();
        titleLogoGlyphs_[i].delay = layout.delay;
    }
}

void TitleScene::InitializeMainMenuUI() {
    if (startTextSprite_) {
        startTextSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        startTextSprite_->SetTextureRect(kMenuStartTextureLeftTop, kMenuStartTextureSize);
        startTextSprite_->SetColor({ 0.41f, 0.91f, 1.0f, 0.0f });
    }
    if (settingTextSprite_) {
        settingTextSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        settingTextSprite_->SetTextureRect(kMenuSettingTextureLeftTop, kMenuSettingTextureSize);
        settingTextSprite_->SetColor({ 0.62f, 0.74f, 0.82f, 0.0f });
    }

    mainMenuStartFrameSprite_ = GetSpriteByName("mainMenuStartFrame");
    mainMenuSettingFrameSprite_ = GetSpriteByName("mainMenuSettingFrame");
    mainMenuArrowSprite_ = GetSpriteByName("mainMenuArrow");
    mainMenuCursorSprite_ = GetSpriteByName("mainMenuCursor");
    mainMenuPromptSprite_ = GetSpriteByName("mainMenuPrompt");

    if (mainMenuStartFrameSprite_) {
        mainMenuStartFrameSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        mainMenuStartFrameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    }
    if (mainMenuSettingFrameSprite_) {
        mainMenuSettingFrameSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        mainMenuSettingFrameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    }
    if (mainMenuArrowSprite_) {
        mainMenuArrowSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        mainMenuArrowBaseSize_ = mainMenuArrowSprite_->GetSize();
        mainMenuArrowSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    }
    if (mainMenuCursorSprite_) {
        mainMenuCursorSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        mainMenuCursorBaseSize_ = mainMenuCursorSprite_->GetSize();
        mainMenuCursorSprite_->SetTextureRect(kMenuCursorTextureLeftTop, kMenuCursorTextureSize);
        mainMenuCursorSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    }
    if (mainMenuPromptSprite_) {
        mainMenuPromptSprite_->SetAnchorPoint({ 0.5f, 0.5f });
        mainMenuPromptBasePosition_ = mainMenuPromptSprite_->GetPosition();
        mainMenuPromptBaseSize_ = mainMenuPromptSprite_->GetSize();
        mainMenuPromptSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    }

    const Vector2 startFramePosition = mainMenuStartFrameSprite_
        ? mainMenuStartFrameSprite_->GetPosition()
        : kMenuStartFramePosition;
    if (mainMenuArrowSprite_) {
        const Vector2 arrowPosition = mainMenuArrowSprite_->GetPosition();
        mainMenuArrowOffset_ = { arrowPosition.x - startFramePosition.x, arrowPosition.y - startFramePosition.y };
    }
    if (mainMenuCursorSprite_) {
        const Vector2 cursorPosition = mainMenuCursorSprite_->GetPosition();
        mainMenuCursorOffset_ = { cursorPosition.x - startFramePosition.x, cursorPosition.y - startFramePosition.y };
    }
}

void TitleScene::UpdateTitleLogoIntro(float deltaTime) {
    if (titleTextSprite_) {
        titleTextSprite_->SetVisible(false);
    }

    const bool settingsOpen = settingsOverlay_ && settingsOverlay_->IsActive();
    const bool showLogo = titleMode_ == TitleMode::MainMenu && !settingsOpen;
    if (!showLogo) {
        for (auto& glyph : titleLogoGlyphs_) {
            if (glyph.sprite) {
                glyph.sprite->SetVisible(false);
            }
        }
        return;
    }

    titleIntroTime_ += deltaTime;
    if (!titleIntroComplete_) {
        if (titleIntroTime_ >= kTitleIntroMenuDelay) {
            titleIntroComplete_ = true;
        }
    }

    for (auto& glyph : titleLogoGlyphs_) {
        Sprite* sprite = glyph.sprite;
        if (!sprite) continue;

        const float localTime = titleIntroTime_ - glyph.delay;
        if (localTime < 0.0f) {
            sprite->SetVisible(false);
            continue;
        }

        const float appearT = std::clamp(localTime / kTitleIntroGlyphDuration, 0.0f, 1.0f);
        const float smooth = SmoothStep(appearT);
        const float bounce = std::sin(appearT * kTitleIntroPi) * (1.0f - appearT * 0.35f);
        const float scaleX = 0.68f + 0.32f * smooth + bounce * 0.10f;
        const float scaleY = 0.55f + 0.45f * smooth + bounce * 0.22f;
        const float yOffset = (1.0f - smooth) * 22.0f - bounce * 10.0f;
        const float alpha = std::clamp(localTime / 0.18f, 0.0f, 1.0f);

        sprite->SetVisible(true);
        sprite->SetPosition({ glyph.basePosition.x, glyph.basePosition.y + yOffset });
        sprite->SetSize({ glyph.baseSize.x * scaleX, glyph.baseSize.y * scaleY });
        sprite->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
    }
}

void TitleScene::UpdateTitleHeroAnimation(float deltaTime) {
    (void)deltaTime;

    if (!titleHeroSlime_) {
        titleHeroSlime_ = FindObjectByName(this, kTitleHeroSlimeName);
        if (titleHeroSlime_ && !titleHeroBaseCaptured_) {
            titleHeroBasePosition_ = titleHeroSlime_->GetTranslate();
            titleHeroBaseScale_ = titleHeroSlime_->GetScale();
            titleHeroBaseRotation_ = titleHeroSlime_->GetRotation();
            titleHeroBaseCaptured_ = true;
        }
    }
    if (!titleHeroSlime_ || !titleHeroBaseCaptured_) {
        return;
    }

    const float phase = std::fmod(titleUiTime_ * 0.82f, 1.0f);
    const float jumpT = phase < 0.56f ? phase / 0.56f : 1.0f;
    const float jumpHeight = phase < 0.56f ? std::sin(jumpT * kTitleIntroPi) * 0.46f : 0.0f;
    const float landT = phase >= 0.56f ? std::clamp((phase - 0.56f) / 0.18f, 0.0f, 1.0f) : 0.0f;
    const float squash = std::sin(landT * kTitleIntroPi) * (1.0f - landT * 0.25f);
    const float wobble = std::sin(titleUiTime_ * 3.2f);

    Vector3 position = titleHeroBasePosition_;
    position.y += jumpHeight;

    Vector3 scale = titleHeroBaseScale_;
    scale.x *= 1.0f + squash * 0.16f + std::sin(titleUiTime_ * 4.4f) * 0.018f;
    scale.y *= 1.0f - squash * 0.18f + jumpHeight * 0.045f;
    scale.z *= 1.0f + squash * 0.16f - std::sin(titleUiTime_ * 3.9f) * 0.014f;

    Vector3 rotation = titleHeroBaseRotation_;
    rotation.y += std::sin(titleUiTime_ * 0.8f) * 0.16f;
    rotation.z += wobble * 0.045f;

    titleHeroSlime_->SetTranslate(position);
    titleHeroSlime_->SetScale(scale);
    titleHeroSlime_->SetRotation(rotation);
    titleHeroSlime_->UpdateLocalMatrix();
    titleHeroSlime_->UpdateWorldMatrix();
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

    const float pulse = 0.5f + 0.5f * std::sin(titleUiTime_ * 6.0f);
    const float selectedScale = 1.0f + pulse * 0.018f;
    const float menuAlpha = std::clamp((titleIntroTime_ - kTitleIntroMenuDelay) / kTitleIntroMenuFadeDuration, 0.0f, 1.0f);
    Vector4 normalColor = { 0.24f, 0.22f, 0.17f, 0.98f * menuAlpha };
    Vector4 selectColor = { 0.07f, 0.20f, 0.25f, menuAlpha };

    if (startTextSprite_) {
        const bool selected = currentMenuIndex_ == (int)MenuIndex::GameStart;
        startTextSprite_->SetPosition(startTextBasePosition_);
        startTextSprite_->SetColor(selected ? selectColor : normalColor);
        startTextSprite_->SetSize({
            startTextBaseSize_.x * (selected ? selectedScale : 0.98f),
            startTextBaseSize_.y * (selected ? selectedScale : 0.98f)
        });
    }
    if (settingTextSprite_) {
        const bool selected = currentMenuIndex_ == (int)MenuIndex::Setting;
        settingTextSprite_->SetPosition(settingTextBasePosition_);
        settingTextSprite_->SetColor(selected ? selectColor : normalColor);
        settingTextSprite_->SetSize({
            settingTextBaseSize_.x * (selected ? selectedScale : 0.98f),
            settingTextBaseSize_.y * (selected ? selectedScale : 0.98f)
        });
    }
    const bool startSelected = currentMenuIndex_ == (int)MenuIndex::GameStart;
    const Vector2 startFramePosition = mainMenuStartFrameSprite_
        ? mainMenuStartFrameSprite_->GetPosition()
        : kMenuStartFramePosition;
    const Vector2 settingFramePosition = mainMenuSettingFrameSprite_
        ? mainMenuSettingFrameSprite_->GetPosition()
        : kMenuSettingFramePosition;
    const uint32_t selectedFrame = Sprite::LoadTexture("ui/title/title_menu_button_blue.png");
    const uint32_t normalFrame = Sprite::LoadTexture("ui/title/title_menu_button_beige.png");
    if (mainMenuStartFrameSprite_) {
        mainMenuStartFrameSprite_->SetTextureHandle(startSelected ? selectedFrame : normalFrame);
        mainMenuStartFrameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, (startSelected ? 1.0f : 0.98f) * menuAlpha });
        mainMenuStartFrameSprite_->Update();
    }
    if (mainMenuSettingFrameSprite_) {
        mainMenuSettingFrameSprite_->SetTextureHandle(startSelected ? normalFrame : selectedFrame);
        mainMenuSettingFrameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, (startSelected ? 0.98f : 1.0f) * menuAlpha });
        mainMenuSettingFrameSprite_->Update();
    }
    const Vector2 selectedFramePosition = startSelected ? startFramePosition : settingFramePosition;
    if (mainMenuCursorSprite_) {
        const float bob = std::sin(titleUiTime_ * 7.5f) * 5.0f;
        const float squash = 1.0f + pulse * 0.06f;
        mainMenuCursorSprite_->SetPosition({
            selectedFramePosition.x + mainMenuCursorOffset_.x,
            selectedFramePosition.y + mainMenuCursorOffset_.y + bob
        });
        mainMenuCursorSprite_->SetSize({
            mainMenuCursorBaseSize_.x * squash,
            mainMenuCursorBaseSize_.y * (1.05f - pulse * 0.05f)
        });
        mainMenuCursorSprite_->SetColor({ 1.0f, 1.0f, 1.0f, menuAlpha });
        mainMenuCursorSprite_->Update();
    }
    if (mainMenuArrowSprite_) {
        const float arrowPulse = 1.0f + pulse * 0.06f;
        mainMenuArrowSprite_->SetPosition({
            selectedFramePosition.x + mainMenuArrowOffset_.x + std::sin(titleUiTime_ * 8.0f) * 3.0f,
            selectedFramePosition.y + mainMenuArrowOffset_.y
        });
        mainMenuArrowSprite_->SetSize({
            mainMenuArrowBaseSize_.x * arrowPulse,
            mainMenuArrowBaseSize_.y * arrowPulse
        });
        mainMenuArrowSprite_->SetColor({ 1.0f, 1.0f, 1.0f, menuAlpha });
        mainMenuArrowSprite_->Update();
    }
    if (mainMenuPromptSprite_) {
        const float promptPulse = 0.5f + 0.5f * std::sin(titleUiTime_ * 1.45f);
        const float promptAlpha = (0.38f + promptPulse * 0.55f) * menuAlpha;
        mainMenuPromptSprite_->SetPosition(mainMenuPromptBasePosition_);
        mainMenuPromptSprite_->SetSize(mainMenuPromptBaseSize_);
        mainMenuPromptSprite_->SetColor({ 1.0f, 1.0f, 1.0f, promptAlpha });
        mainMenuPromptSprite_->Update();
    }

    if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
        if (currentMenuIndex_ == (int)MenuIndex::GameStart) {
            titleMode_ = TitleMode::SaveSelect;
            saveSelectMode_ = SaveSelectMode::Browse;
            deleteConfirmIndex_ = 1;
            currentSaveSlotIndex_ = std::clamp(currentSaveSlotIndex_, 0, GameDataManager::kSaveSlotCount - 1);
            saveSelectFocusIndex_ = currentSaveSlotIndex_;
            DebugConsole::GetInstance()->AddLog("[Title] Open save slot select.");
        }
        else if (currentMenuIndex_ == (int)MenuIndex::Setting) {
            DebugConsole::GetInstance()->AddLog("[Title] Open settings overlay.");
            if (settingsOverlay_) {
                settingsOverlay_->SetActive(true);
            }
        }
    }
}

void TitleScene::UpdateSaveSelect() {
    InputManager* input = InputManager::GetInstance();

    const bool moveUp =
        input->IsKeyTriggered(DIK_UP) ||
        input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP);
    const bool moveDown =
        input->IsKeyTriggered(DIK_DOWN) ||
        input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN);
    const bool moveLeft =
        input->IsKeyTriggered(DIK_LEFT) ||
        input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT);
    const bool moveRight =
        input->IsKeyTriggered(DIK_RIGHT) ||
        input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT);

    if (saveSelectMode_ == SaveSelectMode::DeleteConfirm) {
        if (moveLeft || moveRight) {
            deleteConfirmIndex_ = 1 - deleteConfirmIndex_;
        }

        if (input->IsKeyTriggered(DIK_ESCAPE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
            saveSelectMode_ = SaveSelectMode::Browse;
            deleteConfirmIndex_ = 1;
            DebugConsole::GetInstance()->AddLog("[Title] Cancel save delete.");
            return;
        }

        if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
            if (deleteConfirmIndex_ == 0) {
                DeleteSelectedSaveSlot();
            }
            saveSelectMode_ = SaveSelectMode::Browse;
            deleteConfirmIndex_ = 1;
            return;
        }
        return;
    }

    constexpr int kDeleteFocusIndex = GameDataManager::kSaveSlotCount;
    if (moveUp) {
        saveSelectFocusIndex_--;
        if (saveSelectFocusIndex_ < 0) {
            saveSelectFocusIndex_ = kDeleteFocusIndex;
        }
    }

    if (moveDown) {
        saveSelectFocusIndex_++;
        if (saveSelectFocusIndex_ > kDeleteFocusIndex) {
            saveSelectFocusIndex_ = 0;
        }
    }

    if (saveSelectFocusIndex_ < GameDataManager::kSaveSlotCount) {
        if (moveLeft) {
            saveSelectFocusIndex_--;
            if (saveSelectFocusIndex_ < 0) {
                saveSelectFocusIndex_ = GameDataManager::kSaveSlotCount - 1;
            }
        }

        if (moveRight) {
            saveSelectFocusIndex_++;
            if (saveSelectFocusIndex_ >= GameDataManager::kSaveSlotCount) {
                saveSelectFocusIndex_ = 0;
            }
        }

        currentSaveSlotIndex_ = std::clamp(saveSelectFocusIndex_, 0, GameDataManager::kSaveSlotCount - 1);
    }

    if (input->IsKeyTriggered(DIK_ESCAPE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
        titleMode_ = TitleMode::MainMenu;
        saveSelectMode_ = SaveSelectMode::Browse;
        saveSelectFocusIndex_ = currentSaveSlotIndex_;
        deleteConfirmIndex_ = 1;
        DebugConsole::GetInstance()->AddLog("[Title] Close save slot select.");
        return;
    }

    const GameDataManager::SaveSlotSummary summary = GameDataManager::GetInstance()->GetSlotSummary(currentSaveSlotIndex_);
    const bool requestDelete =
        ((input->IsKeyTriggered(DIK_D) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_X)) && summary.exists) ||
        (saveSelectFocusIndex_ == kDeleteFocusIndex && summary.exists &&
            (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)));

    if (requestDelete) {
        saveSelectMode_ = SaveSelectMode::DeleteConfirm;
        deleteConfirmIndex_ = 1;
        DebugConsole::GetInstance()->AddLog("[Title] Open save delete confirm.");
        return;
    }

    if (saveSelectFocusIndex_ < GameDataManager::kSaveSlotCount &&
        (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A))) {
        StartSelectedSaveSlot();
    }
}

void TitleScene::InitializeSaveSlotUI() {
    titleUiSprites_.clear();
    saveSlotCards_.fill(nullptr);
    for (auto& frame : saveSlotFrames_) {
        frame.fill(nullptr);
    }
    saveSlotIcons_.fill(nullptr);
    saveSlotNumberSprites_.fill(nullptr);
    saveSlotFileNameTexts_.fill(nullptr);
    saveSlotStatusTexts_.fill(nullptr);
    saveSlotLifeIcons_.fill(nullptr);
    saveSlotLifeXIcons_.fill(nullptr);
    for (auto& digits : saveSlotLifeDigits_) {
        digits.fill(nullptr);
    }
    saveSlotCrownIcons_.fill(nullptr);
    saveSlotCrownXIcons_.fill(nullptr);
    for (auto& digits : saveSlotCrownDigits_) {
        digits.fill(nullptr);
    }
    saveSlotStarIcons_.fill(nullptr);
    saveSlotStarXIcons_.fill(nullptr);
    for (auto& digits : saveSlotStarDigits_) {
        digits.fill(nullptr);
    }
    saveSlotPlayTimeLabels_.fill(nullptr);
    for (auto& digits : saveSlotPlayTimeDigits_) {
        digits.fill(nullptr);
    }
    saveSelectHeader_ = nullptr;
    saveDeleteButtonBack_ = nullptr;
    saveDeleteButtonText_ = nullptr;
    savePromptBubble_ = nullptr;
    savePromptText_ = nullptr;
    saveDeleteQuestionText_ = nullptr;
    saveConfirmYesText_ = nullptr;
    saveConfirmBackText_ = nullptr;

    const float screenW = static_cast<float>(WinApp::kClientWidth);
    const float screenH = static_cast<float>(WinApp::kClientHeight);
    const float centerX = screenW * 0.50f;
    const float firstY = screenH * 0.21f;
    const float slotStep = 172.0f;

    auto bindSprite = [&](Sprite*& target, const std::string& name, const std::string& texture, const Vector2& position, const Vector2& size, const Vector4& color) {
        target = GetSpriteByName(name);
        if (!target) {
            const std::string fullTexture = texture.rfind("Resources/", 0) == 0 ? texture : "Resources/sprite/" + texture;
            target = CreateUISprite(fullTexture, position, size, color);
            target->SetName(name);
            target->SetTextureName(texture);
        }
        target->SetVisible(false);
    };

    saveSelectHeader_ = GetSpriteByName("saveSelectHeaderLine");
    if (saveSelectHeader_) {
        saveSelectHeader_->SetVisible(false);
    }

    for (int i = 0; i < GameDataManager::kSaveSlotCount; ++i) {
        const Vector2 pos = { centerX, firstY + slotStep * static_cast<float>(i) };
        const std::string suffix = "_" + std::to_string(i);

        bindSprite(saveSlotCards_[i], "saveSlotCard" + suffix, kSaveSlotCard, pos, { 1120.0f, 162.0f }, { 1.0f, 1.0f, 1.0f, 0.92f });
        bindSprite(saveSlotNumberSprites_[i], "saveSlotNumber" + suffix, "number/big1.png", { pos.x - 470.0f, pos.y - 2.0f }, { 72.0f, 106.0f }, { 1.0f, 0.98f, 0.78f, 1.0f });
        bindSprite(saveSlotFileNameTexts_[i], "saveSlotFileName" + suffix, kFileTextPaths[i], { pos.x - 260.0f, pos.y - 43.0f }, { 226.0f, 87.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
        bindSprite(saveSlotStatusTexts_[i], "saveSlotStatus" + suffix, kTextStartFromBeginning, { pos.x - 260.0f, pos.y + 38.0f }, { 210.0f, 92.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
        bindSprite(saveSlotIcons_[i], "saveSlotIcon" + suffix, kSlimeIcon, { pos.x - 365.0f, pos.y }, { 128.0f, 128.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
        bindSprite(saveSlotCrownIcons_[i], "saveSlotCrownIcon" + suffix, kCrownIcon, { pos.x + 65.0f, pos.y - 38.0f }, { 58.0f, 58.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
        bindSprite(saveSlotCrownXIcons_[i], "saveSlotCrownX" + suffix, kXIcon, { pos.x + 114.0f, pos.y - 38.0f }, { 36.0f, 36.0f }, { 1.0f, 0.98f, 0.70f, 1.0f });
        for (int digit = 0; digit < 2; ++digit) {
            bindSprite(saveSlotCrownDigits_[i][digit], "saveSlotCrownDigit" + std::to_string(digit) + suffix, "number/0.png", { pos.x + 153.0f + digit * 31.0f, pos.y - 38.0f }, { 28.0f, 42.0f }, { 1.0f, 0.95f, 0.55f, 1.0f });
        }

        bindSprite(saveSlotStarIcons_[i], "saveSlotStarIcon" + suffix, kStarIcon, { pos.x + 65.0f, pos.y + 28.0f }, { 54.0f, 54.0f }, { 1.0f, 1.0f, 1.0f, 1.0f });
        bindSprite(saveSlotStarXIcons_[i], "saveSlotStarX" + suffix, kXIcon, { pos.x + 114.0f, pos.y + 28.0f }, { 36.0f, 36.0f }, { 1.0f, 0.98f, 0.70f, 1.0f });
        for (int digit = 0; digit < 2; ++digit) {
            bindSprite(saveSlotStarDigits_[i][digit], "saveSlotStarDigit" + std::to_string(digit) + suffix, "number/0.png", { pos.x + 153.0f + digit * 31.0f, pos.y + 28.0f }, { 28.0f, 42.0f }, { 1.0f, 0.95f, 0.55f, 1.0f });
        }

        bindSprite(saveSlotLifeIcons_[i], "saveSlotLifeIcon" + suffix, kSlimeIcon, { pos.x + 355.0f, pos.y + 28.0f }, { 56.0f, 56.0f }, { 1.0f, 1.0f, 1.0f, 0.95f });
        bindSprite(saveSlotLifeXIcons_[i], "saveSlotLifeX" + suffix, kXIcon, { pos.x + 404.0f, pos.y + 28.0f }, { 36.0f, 36.0f }, { 1.0f, 0.95f, 0.58f, 0.95f });
        for (int digit = 0; digit < 2; ++digit) {
            bindSprite(saveSlotLifeDigits_[i][digit], "saveSlotLifeDigit" + std::to_string(digit) + suffix, "number/0.png", { pos.x + 443.0f + digit * 31.0f, pos.y + 28.0f }, { 28.0f, 42.0f }, { 1.0f, 0.94f, 0.56f, 1.0f });
        }

        bindSprite(saveSlotPlayTimeLabels_[i], "saveSlotPlayTimeLabel" + suffix, kTextPlayTime, { pos.x + 325.0f, pos.y - 38.0f }, { 150.0f, 54.0f }, { 1.0f, 1.0f, 1.0f, 0.86f });
        for (int digit = 0; digit < 5; ++digit) {
            const char* texture = digit == 2 ? "number/colon.png" : "number/0.png";
            bindSprite(saveSlotPlayTimeDigits_[i][digit], "saveSlotPlayTimeDigit" + std::to_string(digit) + suffix, texture, { pos.x + 418.0f + digit * 24.0f, pos.y - 38.0f }, digit == 2 ? Vector2{ 13.0f, 38.0f } : Vector2{ 21.0f, 38.0f }, { 1.0f, 0.95f, 0.70f, 0.95f });
        }
    }

    bindSprite(saveDeleteButtonBack_, "saveDeleteButtonBack", kSaveDeleteButton, { screenW * 0.25f, screenH * 0.82f }, { 430.0f, 86.0f }, { 1.0f, 1.0f, 1.0f, 0.88f });
    bindSprite(saveDeleteButtonText_, "saveDeleteButtonText", kTextDeleteFile, { screenW * 0.25f, screenH * 0.82f }, { 278.0f, 74.0f }, { 1.0f, 1.0f, 1.0f, 0.96f });
    bindSprite(savePromptBubble_, "savePromptBubble", "ui/title/save_prompt_bubble.png", { screenW * 0.70f, screenH * 0.82f }, { 720.0f, 190.0f }, { 1.0f, 1.0f, 1.0f, 0.92f });
    bindSprite(savePromptText_, "savePromptText", kTextStartQuestion, { screenW * 0.70f, screenH * 0.78f }, { 520.0f, 83.0f }, { 0.28f, 0.20f, 0.14f, 1.0f });
    bindSprite(saveDeleteQuestionText_, "saveDeleteQuestionText", kTextDeleteQuestion, { screenW * 0.70f, screenH * 0.76f }, { 300.0f, 92.0f }, { 0.28f, 0.20f, 0.14f, 1.0f });
    bindSprite(saveConfirmYesText_, "saveConfirmYesText", kTextYes, { screenW * 0.64f, screenH * 0.85f }, { 92.0f, 72.0f }, { 0.28f, 0.20f, 0.14f, 1.0f });
    bindSprite(saveConfirmBackText_, "saveConfirmBackText", kTextBack, { screenW * 0.77f, screenH * 0.85f }, { 132.0f, 74.0f }, { 0.28f, 0.20f, 0.14f, 1.0f });
}

void TitleScene::SetNumberSprites(std::array<Sprite*, 2>& digits, int value, const Vector4& color, bool visible) {
    value = std::clamp(value, 0, 99);

    std::array<int, 2> digitValues = { value / 10, value % 10 };
    const int digitCount = value >= 10 ? 2 : 1;

    for (int i = 0; i < 2; ++i) {
        Sprite* sprite = digits[i];
        if (!sprite) continue;

        const bool digitVisible = visible && (digitCount == 2 || i == 1);
        sprite->SetVisible(digitVisible);
        if (!digitVisible) {
            continue;
        }

        const int sourceIndex = digitCount == 2 ? i : 1;
        const int digit = digitValues[sourceIndex];
        const uint32_t handle = Sprite::LoadTexture("number/" + std::to_string(digit) + ".png");
        sprite->SetTextureHandle(handle);
        sprite->SetColor(color);
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
    const bool deleteConfirm = inSaveSelect && saveSelectMode_ == SaveSelectMode::DeleteConfirm;
    const bool settingsOpen = settingsOverlay_ && settingsOverlay_->IsActive();
    const bool showMainMenu = !settingsOpen && !inSaveSelect && titleIntroComplete_;
    if (titleTextSprite_) titleTextSprite_->SetVisible(false);
    if (startTextSprite_) startTextSprite_->SetVisible(showMainMenu);
    if (settingTextSprite_) settingTextSprite_->SetVisible(showMainMenu);
    if (mainMenuStartFrameSprite_) mainMenuStartFrameSprite_->SetVisible(showMainMenu);
    if (mainMenuSettingFrameSprite_) mainMenuSettingFrameSprite_->SetVisible(showMainMenu);
    if (mainMenuArrowSprite_) mainMenuArrowSprite_->SetVisible(showMainMenu);
    if (mainMenuCursorSprite_) mainMenuCursorSprite_->SetVisible(showMainMenu);
    if (mainMenuPromptSprite_) mainMenuPromptSprite_->SetVisible(showMainMenu);
    if (saveSelectHeader_) saveSelectHeader_->SetVisible(false);

    const float pulse = 0.5f + 0.5f * std::sin(titleUiTime_ * 5.0f);

    for (int i = 0; i < GameDataManager::kSaveSlotCount; ++i) {
        const bool selected = inSaveSelect && !deleteConfirm && saveSelectFocusIndex_ == i;
        const bool deleteTarget = inSaveSelect && deleteConfirm && i == currentSaveSlotIndex_;
        const GameDataManager::SaveSlotSummary summary = GameDataManager::GetInstance()->GetSlotSummary(i);
        const bool emphasized = selected || deleteTarget;
        const Vector4 cardColor = emphasized
            ? Vector4{ 1.0f, 1.0f, 1.0f, 0.86f + pulse * 0.14f }
            : Vector4{ 1.0f, 1.0f, 1.0f, summary.exists ? 0.82f : 0.58f };
        const Vector4 iconColor = summary.exists
            ? Vector4{ 1.0f, 1.0f, 1.0f, emphasized ? 1.0f : 0.82f }
            : Vector4{ 0.95f, 0.95f, 0.95f, emphasized ? 0.64f : 0.36f };
        const Vector4 numberColor = emphasized
            ? Vector4{ 1.0f, 0.96f + pulse * 0.04f, 0.50f, 1.0f }
            : Vector4{ 1.0f, 0.88f, 0.62f, summary.exists ? 0.80f : 0.42f };
        const Vector4 textColor = emphasized
            ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }
            : Vector4{ 1.0f, 1.0f, 1.0f, summary.exists ? 0.82f : 0.48f };

        if (saveSlotCards_[i]) {
            saveSlotCards_[i]->SetVisible(inSaveSelect);
            const char* cardTexture = emphasized ? kSaveSlotCardSelected : (summary.exists ? kSaveSlotCard : kSaveSlotCardEmpty);
            saveSlotCards_[i]->SetTextureHandle(Sprite::LoadTexture(cardTexture));
            saveSlotCards_[i]->SetColor(cardColor);
        }

        for (auto* frame : saveSlotFrames_[i]) {
            if (frame) {
                frame->SetVisible(false);
            }
        }

        if (saveSlotNumberSprites_[i]) {
            const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(i + 1) + ".png");
            saveSlotNumberSprites_[i]->SetTextureHandle(handle);
            saveSlotNumberSprites_[i]->SetVisible(inSaveSelect);
            saveSlotNumberSprites_[i]->SetColor(numberColor);
        }

        if (saveSlotFileNameTexts_[i]) {
            saveSlotFileNameTexts_[i]->SetVisible(inSaveSelect);
            saveSlotFileNameTexts_[i]->SetColor(textColor);
        }

        if (saveSlotStatusTexts_[i]) {
            const uint32_t handle = Sprite::LoadTexture(summary.exists
                ? "generated/text/text_text_46356cfa.png"
                : "generated/text/text_text_c8a6dc24.png");
            saveSlotStatusTexts_[i]->SetTextureHandle(handle);
            saveSlotStatusTexts_[i]->SetVisible(inSaveSelect);
            saveSlotStatusTexts_[i]->SetColor(textColor);
        }

        if (saveSlotIcons_[i]) {
            saveSlotIcons_[i]->SetVisible(inSaveSelect);
            saveSlotIcons_[i]->SetColor(iconColor);
        }

        if (saveSlotCrownIcons_[i]) {
            saveSlotCrownIcons_[i]->SetVisible(inSaveSelect && summary.exists);
            saveSlotCrownIcons_[i]->SetColor(iconColor);
        }
        if (saveSlotCrownXIcons_[i]) {
            saveSlotCrownXIcons_[i]->SetVisible(inSaveSelect && summary.exists);
            saveSlotCrownXIcons_[i]->SetColor(numberColor);
        }
        SetNumberSprites(
            saveSlotCrownDigits_[i],
            summary.clearedStageCount,
            numberColor,
            inSaveSelect && summary.exists
        );

        if (saveSlotStarIcons_[i]) {
            saveSlotStarIcons_[i]->SetVisible(inSaveSelect && summary.exists);
            saveSlotStarIcons_[i]->SetColor(iconColor);
        }
        if (saveSlotStarXIcons_[i]) {
            saveSlotStarXIcons_[i]->SetVisible(inSaveSelect && summary.exists);
            saveSlotStarXIcons_[i]->SetColor(numberColor);
        }
        SetNumberSprites(
            saveSlotStarDigits_[i],
            summary.collectedStarCoins,
            numberColor,
            inSaveSelect && summary.exists
        );

        if (saveSlotLifeIcons_[i]) {
            saveSlotLifeIcons_[i]->SetVisible(inSaveSelect && summary.exists);
            saveSlotLifeIcons_[i]->SetColor(iconColor);
        }
        if (saveSlotLifeXIcons_[i]) {
            saveSlotLifeXIcons_[i]->SetVisible(inSaveSelect && summary.exists);
            saveSlotLifeXIcons_[i]->SetColor(numberColor);
        }

        SetNumberSprites(
            saveSlotLifeDigits_[i],
            summary.lives,
            numberColor,
            inSaveSelect && summary.exists
        );

        if (saveSlotPlayTimeLabels_[i]) {
            saveSlotPlayTimeLabels_[i]->SetVisible(inSaveSelect && summary.exists);
            saveSlotPlayTimeLabels_[i]->SetColor(textColor);
        }

        const int totalSeconds = std::clamp(summary.playTimeSeconds, 0, 99 * 60 + 59);
        const int minutes = totalSeconds / 60;
        const int seconds = totalSeconds % 60;
        const std::array<int, 4> timeDigits = { minutes / 10, minutes % 10, seconds / 10, seconds % 10 };
        int digitIndex = 0;
        for (int part = 0; part < 5; ++part) {
            Sprite* sprite = saveSlotPlayTimeDigits_[i][part];
            if (!sprite) continue;

            const bool visible = inSaveSelect && summary.exists;
            sprite->SetVisible(visible);
            if (!visible) continue;

            if (part == 2) {
                sprite->SetTextureHandle(Sprite::LoadTexture("number/colon.png"));
            } else {
                const int value = timeDigits[digitIndex++];
                sprite->SetTextureHandle(Sprite::LoadTexture("number/" + std::to_string(value) + ".png"));
            }
            sprite->SetColor(numberColor);
        }
    }

    const GameDataManager::SaveSlotSummary selectedSummary = GameDataManager::GetInstance()->GetSlotSummary(currentSaveSlotIndex_);
    const bool deleteFocused = inSaveSelect && !deleteConfirm && saveSelectFocusIndex_ == GameDataManager::kSaveSlotCount;
    const Vector4 enabledDeleteColor = selectedSummary.exists
        ? (deleteFocused ? Vector4{ 1.0f, 1.0f, 1.0f, 0.86f + pulse * 0.14f } : Vector4{ 1.0f, 1.0f, 1.0f, 0.86f })
        : Vector4{ 0.42f, 0.42f, 0.42f, 0.34f };
    if (saveDeleteButtonBack_) {
        saveDeleteButtonBack_->SetVisible(inSaveSelect && !deleteConfirm);
        saveDeleteButtonBack_->SetTextureHandle(Sprite::LoadTexture(deleteFocused ? kSaveDeleteButtonSelected : kSaveDeleteButton));
        saveDeleteButtonBack_->SetColor(enabledDeleteColor);
    }
    if (saveDeleteButtonText_) {
        saveDeleteButtonText_->SetVisible(inSaveSelect && !deleteConfirm);
        saveDeleteButtonText_->SetColor(selectedSummary.exists
            ? Vector4{ 1.0f, 1.0f, 1.0f, 0.96f }
            : Vector4{ 0.68f, 0.68f, 0.68f, 0.42f });
    }

    if (savePromptBubble_) {
        savePromptBubble_->SetVisible(inSaveSelect);
        savePromptBubble_->SetColor(deleteConfirm
            ? Vector4{ 1.0f, 0.86f, 0.64f, 0.94f }
            : Vector4{ 1.0f, 0.95f, 0.72f, 0.90f });
    }
    if (savePromptText_) {
        savePromptText_->SetVisible(inSaveSelect && !deleteConfirm && !deleteFocused);
    }
    if (saveDeleteQuestionText_) {
        saveDeleteQuestionText_->SetVisible(deleteConfirm || deleteFocused);
    }
    if (saveConfirmYesText_) {
        saveConfirmYesText_->SetVisible(deleteConfirm);
        saveConfirmYesText_->SetColor(deleteConfirmIndex_ == 0
            ? Vector4{ 0.08f, 0.44f + pulse * 0.18f, 0.96f, 1.0f }
            : Vector4{ 0.28f, 0.20f, 0.14f, 0.70f });
    }
    if (saveConfirmBackText_) {
        saveConfirmBackText_->SetVisible(deleteConfirm);
        saveConfirmBackText_->SetColor(deleteConfirmIndex_ == 1
            ? Vector4{ 0.08f, 0.44f + pulse * 0.18f, 0.96f, 1.0f }
            : Vector4{ 0.28f, 0.20f, 0.14f, 0.70f });
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

void TitleScene::DeleteSelectedSaveSlot() {
    GameDataManager* saveData = GameDataManager::GetInstance();
    const GameDataManager::SaveSlotSummary summary = saveData->GetSlotSummary(currentSaveSlotIndex_);
    if (!summary.exists) {
        DebugConsole::GetInstance()->AddLog("[Title] Delete skipped: save slot is empty.");
        return;
    }

    const bool deleted = saveData->DeleteSlot(currentSaveSlotIndex_);
    DebugConsole::GetInstance()->AddLog(deleted ? "[Title] Save slot deleted." : "[Title] Save slot delete failed.");
}

void TitleScene::DrawSaveSlotUI() {
    for (auto& sprite : titleUiSprites_) {
        sprite->Draw();
    }
}

void TitleScene::Draw() {
    // --- 一人称視点判定 ---
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
#ifndef _DEBUG
    if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
    if (skybox_ && camera && LightManager::GetInstance()->IsSkyboxEnabled()) {
        skybox_->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
        skybox_->Draw(camera->GetConstantBuffer());
    }

    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    // --- 1. 不透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || IsSpecialMaterialType(obj->GetMaterialType())) continue; // フォグ(7)も不透明パスから除外
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

    DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);
    const bool grabUpdated = DrawSpecialMaterialObjects(objects, dxCommon_, BulletManager::GetInstance(), player_, isFirstPerson);
    DrawGPUParticles(dxCommon_, camera, gpuParticleTexHandle_, grabUpdated);
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
    if (settingsOverlay_) {
        settingsOverlay_->Draw();
    }
}

// シャドウマップ描画の実装
void TitleScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

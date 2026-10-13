#define NOMINMAX
#include "game/scene/title/TitleScene.h"
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
#include "SceneManager.h"
#include "DebugConsole.h"
#include "BulletManager.h"
#include "LevelLoader.h"
#include "GameRule.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "LightEditor.h"
#include "ParticleManager.h"
#include "GPUParticleManager.h"
#include "GameDataManager.h"
#include "WinApp.h"
#include "Fade.h"

#include <algorithm>
#include <cmath>

namespace {
// 全体画像の切り替え時はUV範囲も更新します。共通Spriteのアトラス用設定は変更しません。
void SetFullSaveUiTexture(Sprite* sprite, uint32_t handle, bool keepTextScale = false) {
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
    if (keepTextScale && sprite->GetTextureHandle() != handle) {
        const auto& previous = TextureManager::GetInstance()->GetMetadata(sprite->GetTextureHandle());
        if (previous.width > 0 && previous.height > 0) {
            const Vector2 size = sprite->GetSize();
            sprite->SetSize({size.x * static_cast<float>(metadata.width) / static_cast<float>(previous.width),
                             size.y * static_cast<float>(metadata.height) / static_cast<float>(previous.height)});
        }
    }
    sprite->SetTextureHandle(handle);
    if (keepTextScale) {
        // 生成文字は上下に大きな透明余白がある。文字部分を使い、縮小表示でも読める大きさにする。
        sprite->SetTextureRect({16.0f, 34.0f},
                               {static_cast<float>(metadata.width) - 32.0f, static_cast<float>(metadata.height) - 68.0f});
    } else {
        sprite->SetTextureRect({0.0f, 0.0f}, {static_cast<float>(metadata.width), static_cast<float>(metadata.height)});
    }
}

constexpr const char* kTextFile1 = "Resources/sprite/generated/text/text_1_3c19efbf.png";
constexpr const char* kTextFile2 = "Resources/sprite/generated/text/text_2_3d19f152.png";
constexpr const char* kTextFile3 = "Resources/sprite/generated/text/text_3_3e19f2e5.png";
constexpr const char* kTextStartFromBeginning = "Resources/sprite/ui/title/save_beginning_label.png";
constexpr const char* kTextContinue = "Resources/sprite/generated/text/text_text_46356cfa.png";
constexpr const char* kTextDeleteFile = "Resources/sprite/generated/text/text_text_1f05b6eb.png";
constexpr const char* kTextDeleteQuestion = "Resources/sprite/ui/title/save_delete_question.png";
constexpr const char* kTextYes = "Resources/sprite/generated/text/text_text_a214f5c6.png";
constexpr const char* kTextBack = "Resources/sprite/generated/text/text_text_70188c3d.png";
constexpr const char* kTextPlayTime = "Resources/sprite/generated/text/text_text_46271c76.png";
constexpr const char* kCrownIcon = "Resources/sprite/ui/title/crown_progress_icon.png";
constexpr const char* kStarIcon = "Resources/sprite/ui/hud/stage_star_filled.png";
constexpr const char* kLivesLabel = "Resources/sprite/ui/title/save_lives_label.png";
constexpr const char* kXIcon = "Resources/sprite/ui/hud/xUi.png";
constexpr const char* kWhite = "Resources/sprite/common/white.png";
constexpr const char* kSaveSlotCard = "ui/title/save_slot_card.png";
constexpr const char* kSaveSlotCardSelected = "ui/title/save_slot_card_selected.png";
constexpr const char* kSaveSlotCardEmpty = "ui/title/save_slot_card_empty.png";
constexpr const char* kSaveDeleteButton = "ui/title/save_delete_button.png";
constexpr const char* kSaveDeleteButtonSelected = "ui/title/save_delete_button_selected.png";
constexpr const char* kTitleHeroSlimeName = "TitleHeroSlime";
constexpr const char* kTitleBackgroundLayoutPath = "Resources/json/3Dobject/stageSelect_object.json";
constexpr const char* kTitleSkyboxPath = "Resources/output_skybox.dds";
constexpr const char* kTitleLightPath = "Resources/json/light/stageSelect.json";
constexpr float kTitleIntroLastDelay = 0.385f;
constexpr float kTitleIntroGlyphDuration = 0.45f;
constexpr float kTitleIntroMenuDelay = kTitleIntroLastDelay + kTitleIntroGlyphDuration + 0.20f;
constexpr float kTitleIntroMenuFadeDuration = 0.35f;
constexpr float kSaveLaunchDuration = 0.70f;
constexpr float kTitleIntroPi = 3.1415926535f;
const Vector2 kMenuStartFramePosition = {1536.0f, 712.0f};
const Vector2 kMenuSettingFramePosition = {1536.0f, 818.0f};
const Vector2 kMenuStartTextureLeftTop = {2.0f, 2.0f};
const Vector2 kMenuStartTextureSize = {278.0f, 42.0f};
const Vector2 kMenuSettingTextureLeftTop = {5.0f, 3.0f};
const Vector2 kMenuSettingTextureSize = {75.0f, 37.0f};
const Vector2 kMenuCursorTextureLeftTop = {115.0f, 218.0f};
const Vector2 kMenuCursorTextureSize = {1024.0f, 827.0f};
const Vector3 kTitleHeroDefaultPosition = {0.0f, -0.42f, 5.5f};
const Vector3 kTitleHeroDefaultScale = {3.0f, 3.0f, 3.0f};
constexpr float kTitleHeroSequenceDuration = 22.0f;
constexpr float kTitleHeroPatrolStart = 4.8f;
constexpr float kTitleHeroPatrolEnd = 17.6f;
const std::array<Vector3, 7> kTitleHeroPatrolOffsets = {
    Vector3{0.0f, 0.0f, 0.0f}, Vector3{-4.8f, 0.0f, -1.2f}, Vector3{-5.6f, 0.0f, 4.1f}, Vector3{-1.4f, 0.0f, 6.4f},
    Vector3{4.6f, 0.0f, 4.6f}, Vector3{5.2f, 0.0f, -1.8f},  Vector3{0.0f, 0.0f, 0.0f}};

const std::array<const char*, 3> kFileTextPaths = {kTextFile1, kTextFile2, kTextFile3};

// タイトル一文字分の画像と初期配置、出現をずらす遅延秒。
struct TitleLogoGlyphLayout {
    const char* name;
    const char* texture;
    Vector2 position;
    Vector2 size;
    float delay;
};

const std::array<TitleLogoGlyphLayout, 8> kTitleLogoGlyphLayouts = {
    TitleLogoGlyphLayout{
        "titleLogoChar_0", "title/logo/slime_adventure_char_00_su.png", {596.5f, 147.3f}, {102.8f, 101.1f}, 0.000f},
    TitleLogoGlyphLayout{
        "titleLogoChar_1", "title/logo/slime_adventure_char_01_ra.png", {699.6f, 142.7f}, {101.6f, 102.8f}, 0.055f},
    TitleLogoGlyphLayout{
        "titleLogoChar_2", "title/logo/slime_adventure_char_02_i.png", {801.8f, 146.3f}, {103.7f, 104.1f}, 0.110f},
    TitleLogoGlyphLayout{
        "titleLogoChar_3", "title/logo/slime_adventure_char_03_mu.png", {902.3f, 147.7f}, {108.3f, 102.8f}, 0.165f},
    TitleLogoGlyphLayout{
        "titleLogoChar_4", "title/logo/slime_adventure_char_04_no.png", {1018.5f, 139.1f}, {107.4f, 101.6f}, 0.220f},
    TitleLogoGlyphLayout{
        "titleLogoChar_5", "title/logo/slime_adventure_char_05_dai.png", {1114.9f, 147.3f}, {109.5f, 110.3f}, 0.275f},
    TitleLogoGlyphLayout{
        "titleLogoChar_6", "title/logo/slime_adventure_char_06_bou.png", {1221.7f, 150.6f}, {104.9f, 109.5f}, 0.330f},
    TitleLogoGlyphLayout{
        "titleLogoChar_7", "title/logo/slime_adventure_char_07_ken.png", {1327.3f, 147.5f}, {110.4f, 111.6f}, 0.385f}};

float SmoothStep(float t) {
    // タイトル文字やカードの登場で始点・終点の急な速度変化を抑える。
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vector3 LerpTitleVector3(const Vector3& from, const Vector3& to, float t) {
    // 巡回経路の区間外へ飛び出さないよう補間率を丸める。
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return {from.x + (to.x - from.x) * clamped, from.y + (to.y - from.y) * clamped, from.z + (to.z - from.z) * clamped};
}

bool IsFadePlayingForSceneIntro() {
    // シーン遷移のフェード中はタイトル演出の開始時刻を進めない。
    const Fade::Status status = Fade::GetInstance()->GetStatus();
    return status == Fade::Status::FadeIn || status == Fade::Status::FadeOut || status == Fade::Status::IrisIn ||
           status == Fade::Status::IrisOut;
}

Object3d* FindObjectByName(BaseScene* scene, const std::string& name) {
    // 配置 JSON に主人公がない場合は、呼び出し元が実行時モデルを生成できる。
    if (!scene)
        return nullptr;
    for (auto& object : scene->GetObjects()) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

} // namespace

// タイトル演出、メニュー、セーブ枠選択を順に扱います。選択した枠を確定してからシーン遷移を要求します。

SceneLoadManifest TitleScene::BuildAsyncLoadManifest() const {
    // 保存選択へ入る前に必要な 3D 背景と Sprite レイアウトを先読みする。
    SceneLoadManifest manifest;
    manifest.AddObjectLayout(kTitleBackgroundLayoutPath);
    manifest.AddSpriteLayout(HasSceneAssetContext() && !GetSceneLoadContext().spriteLayoutPath.empty()
                                 ? GetSceneLoadContext().spriteLayoutPath
                                 : "Resources/json/sprite/titleScene.json");
    manifest.AddModel("Characters/player");
    manifest.AddModel("Characters/slime");
    manifest.AddModel("Samples/teapot");
    manifest.AddTexture("Resources/sprite/common/white.png");
    manifest.AddTexture(kTitleSkyboxPath);
    return manifest;
}

void TitleScene::Initialize() {
    // --- 1. システム基盤の取得 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();

    // --- 2. モデルのプリロード ---
    ModelManager::GetInstance()->LoadModel("Characters/player");
    ModelManager::GetInstance()->LoadModel("Characters/slime");
    ModelManager::GetInstance()->LoadModel("Samples/teapot");
    LOG("TitleScene Initialized!");


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

    skyboxTextureHandle_ = TextureManager::GetInstance()->Load(kTitleSkyboxPath);
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
    // タイトルUIは維持し、3D背景だけをステージセレクトと共有する。
    levelLoader_->LoadObjectLayout(this, kTitleBackgroundLayoutPath, false);
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/titleScene.json");

    LightManager::GetInstance()->LoadState(kTitleLightPath);
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile(ResolveSceneCameraPath("title_camera.json"));
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);

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
        hero->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
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

        titleCameraAnchor_ = std::make_unique<Object3d>();
        titleCameraAnchor_->Initialize(object3dCommon_.get());
        titleCameraAnchor_->SetName("TitleCameraAnchor");
        titleCameraAnchor_->SetTranslate(titleHeroBasePosition_);
        titleCameraAnchor_->UpdateLocalMatrix();
        titleCameraAnchor_->UpdateWorldMatrix();
    }
    titleHeroSequenceTime_ = 0.0f;
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
    UpdateSaveSlotUI(0.0f);
}

void TitleScene::Finalize() {
    // Sprite の所有コンテナを破棄する前に、そこを指す演出用の参照を無効化する。
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();

    objectManager_.reset();
    titleUiSprites_.clear();
    saveSlotCards_.fill(nullptr);
    for (auto& frame : saveSlotFrames_) {
        frame.fill(nullptr);
    }
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
    saveSelectBackdrop_ = nullptr;
    saveDeleteButtonBack_ = nullptr;
    saveDeleteButtonText_ = nullptr;
    savePromptBubble_ = nullptr;
    saveSelectionGlow_ = nullptr;
    saveHintBackdrop_ = nullptr;
    saveDetailEmptyLabel_ = nullptr;
    saveLaunchLabel_ = nullptr;
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
    titleCameraAnchor_.reset();
    titleHeroBaseCaptured_ = false;
    titleHeroSequenceTime_ = 0.0f;
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
    // 設定・メインメニュー・セーブ選択の入力を分けつつ、背景の演出は継続する。
    titleUiTime_ += deltaTime;
    const float introDeltaTime = IsFadePlayingForSceneIntro() ? 0.0f : deltaTime;
    UpdateTitleLogoIntro(introDeltaTime);

    const bool isSettingsOpen = settingsOverlay_ && settingsOverlay_->IsActive();
    if (isSettingsOpen) {
        settingsOverlay_->Update(deltaTime);
    } else if (titleMode_ == TitleMode::MainMenu) {
        if (!titleIntroComplete_) {
            UpdateSaveSlotUI(deltaTime);
            LightEditor::GetInstance()->Update();
            objectManager_->Update(deltaTime);
            UpdateTitleHeroAnimation(deltaTime);
            CameraEditor::GetInstance()->Update(titleCameraAnchor_ ? titleCameraAnchor_.get() : titleHeroSlime_, false);
            CameraManager::GetInstance()->Update(deltaTime);
            particleSystem_->Update(deltaTime);
            for (auto& sprite : sprites_) {
                sprite->Update();
            }
            if (mainMenuStartFrameSprite_)
                mainMenuStartFrameSprite_->Update();
            if (mainMenuSettingFrameSprite_)
                mainMenuSettingFrameSprite_->Update();
            if (mainMenuArrowSprite_)
                mainMenuArrowSprite_->Update();
            if (mainMenuCursorSprite_)
                mainMenuCursorSprite_->Update();
            if (mainMenuPromptSprite_)
                mainMenuPromptSprite_->Update();
            for (auto& sprite : titleUiSprites_) {
                sprite->Update();
            }
            BulletManager::GetInstance()->Update(deltaTime);
            CollisionManager::GetInstance()->Update();
            return;
        }
        UpdateMainMenu();
    } else {
        UpdateSaveSelect(deltaTime);
    }

    UpdateSaveSlotUI(deltaTime);

    // 常に実行されるマネージャ更新
    LightEditor::GetInstance()->Update();

    // オブジェクト一括更新 (ObjectManagerに委譲)
    objectManager_->Update(deltaTime);
    UpdateTitleHeroAnimation(deltaTime);
    CameraEditor::GetInstance()->Update(titleCameraAnchor_ ? titleCameraAnchor_.get() : titleHeroSlime_, false);
    CameraManager::GetInstance()->Update(deltaTime);
    particleSystem_->Update(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }
    if (mainMenuStartFrameSprite_)
        mainMenuStartFrameSprite_->Update();
    if (mainMenuSettingFrameSprite_)
        mainMenuSettingFrameSprite_->Update();
    if (mainMenuArrowSprite_)
        mainMenuArrowSprite_->Update();
    if (mainMenuCursorSprite_)
        mainMenuCursorSprite_->Update();
    if (mainMenuPromptSprite_)
        mainMenuPromptSprite_->Update();
    for (auto& sprite : titleUiSprites_) {
        sprite->Update();
    }

    // 各種グローバル更新
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
}

void TitleScene::InitializeTitleLogoUI() {
    // ロゴを一文字ずつ分け、エディター配置があればそれを優先する。
    if (titleTextSprite_) {
        titleTextSprite_->SetVisible(false);
    }

    for (size_t i = 0; i < kTitleLogoGlyphLayouts.size(); ++i) {
        const auto& layout = kTitleLogoGlyphLayouts[i];
        Sprite* sprite = GetSpriteByName(layout.name);
        if (!sprite) {
            sprite = CreateUISprite("Resources/sprite/" + std::string(layout.texture), layout.position, layout.size,
                                    {1.0f, 1.0f, 1.0f, 0.0f});
            sprite->SetName(layout.name);
            sprite->SetTextureName(layout.texture);
        }

        sprite->SetAnchorPoint({0.5f, 0.5f});
        sprite->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
        sprite->SetVisible(false);

        titleLogoGlyphs_[i].sprite = sprite;
        titleLogoGlyphs_[i].basePosition = sprite->GetPosition();
        titleLogoGlyphs_[i].baseSize = sprite->GetSize();
        titleLogoGlyphs_[i].delay = layout.delay;
    }
}

void TitleScene::InitializeMainMenuUI() {
    // ボタン枠を基準に、文字・カーソルの相対位置を保存する。
    if (startTextSprite_) {
        startTextSprite_->SetAnchorPoint({0.5f, 0.5f});
        startTextSprite_->SetTextureRect(kMenuStartTextureLeftTop, kMenuStartTextureSize);
        startTextSprite_->SetColor({0.41f, 0.91f, 1.0f, 0.0f});
    }
    if (settingTextSprite_) {
        settingTextSprite_->SetAnchorPoint({0.5f, 0.5f});
        settingTextSprite_->SetTextureRect(kMenuSettingTextureLeftTop, kMenuSettingTextureSize);
        settingTextSprite_->SetColor({0.62f, 0.74f, 0.82f, 0.0f});
    }

    mainMenuStartFrameSprite_ = GetSpriteByName("mainMenuStartFrame");
    mainMenuSettingFrameSprite_ = GetSpriteByName("mainMenuSettingFrame");
    mainMenuArrowSprite_ = GetSpriteByName("mainMenuArrow");
    mainMenuCursorSprite_ = GetSpriteByName("mainMenuCursor");
    mainMenuPromptSprite_ = GetSpriteByName("mainMenuPrompt");

    if (mainMenuStartFrameSprite_) {
        mainMenuStartFrameSprite_->SetAnchorPoint({0.5f, 0.5f});
        mainMenuStartFrameSprite_->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
    }
    if (mainMenuSettingFrameSprite_) {
        mainMenuSettingFrameSprite_->SetAnchorPoint({0.5f, 0.5f});
        mainMenuSettingFrameSprite_->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
    }
    if (mainMenuArrowSprite_) {
        mainMenuArrowSprite_->SetAnchorPoint({0.5f, 0.5f});
        mainMenuArrowBaseSize_ = mainMenuArrowSprite_->GetSize();
        mainMenuArrowSprite_->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
    }
    if (mainMenuCursorSprite_) {
        mainMenuCursorSprite_->SetAnchorPoint({0.5f, 0.5f});
        mainMenuCursorBaseSize_ = mainMenuCursorSprite_->GetSize();
        mainMenuCursorSprite_->SetTextureRect(kMenuCursorTextureLeftTop, kMenuCursorTextureSize);
        mainMenuCursorSprite_->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
    }
    if (mainMenuPromptSprite_) {
        mainMenuPromptSprite_->SetAnchorPoint({0.5f, 0.5f});
        mainMenuPromptBasePosition_ = mainMenuPromptSprite_->GetPosition();
        mainMenuPromptBaseSize_ = mainMenuPromptSprite_->GetSize();
        mainMenuPromptSprite_->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
    }

    const Vector2 startFramePosition =
        mainMenuStartFrameSprite_ ? mainMenuStartFrameSprite_->GetPosition() : kMenuStartFramePosition;
    if (mainMenuArrowSprite_) {
        const Vector2 arrowPosition = mainMenuArrowSprite_->GetPosition();
        mainMenuArrowOffset_ = {arrowPosition.x - startFramePosition.x, arrowPosition.y - startFramePosition.y};
    }
    if (mainMenuCursorSprite_) {
        const Vector2 cursorPosition = mainMenuCursorSprite_->GetPosition();
        mainMenuCursorOffset_ = {cursorPosition.x - startFramePosition.x, cursorPosition.y - startFramePosition.y};
    }
}

void TitleScene::UpdateTitleLogoIntro(float deltaTime) {
    // 文字ごとの遅延を使い、全体ロゴを一度に表示しない。
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
        if (!sprite)
            continue;

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
        sprite->SetPosition({glyph.basePosition.x, glyph.basePosition.y + yOffset});
        sprite->SetSize({glyph.baseSize.x * scaleX, glyph.baseSize.y * scaleY});
        sprite->SetColor({1.0f, 1.0f, 1.0f, alpha});
    }
}

void TitleScene::UpdateTitleHeroAnimation(float deltaTime) {
    // 配置済み位置を基準に、待機・巡回・復帰を一つの周期で繰り返す。
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

    titleHeroSequenceTime_ = std::fmod(titleHeroSequenceTime_ + std::max(0.0f, deltaTime), kTitleHeroSequenceDuration);
    const float time = titleHeroSequenceTime_;

    Vector3 position = titleHeroBasePosition_;
    Vector3 scale = titleHeroBaseScale_;
    Vector3 rotation = titleHeroBaseRotation_;
    float squash = 0.0f;
    float airborneStretch = 0.0f;

    if (time < kTitleHeroPatrolStart) {
        const float idleCycle = std::fmod(time, 2.85f);
        if (idleCycle < 1.08f) {
            const float jumpT = idleCycle / 1.08f;
            const float jump = std::sin(jumpT * kTitleIntroPi);
            position.y += jump * 0.48f;
            airborneStretch = jump * 0.045f;
            squash = std::sin(std::clamp((jumpT - 0.78f) / 0.22f, 0.0f, 1.0f) * kTitleIntroPi) * 0.16f;
        }
        rotation.y += std::sin(time * 0.85f) * 0.15f;
        rotation.z += std::sin(time * 2.9f) * 0.035f;
    } else if (time < kTitleHeroPatrolEnd) {
        const float patrolT =
            std::clamp((time - kTitleHeroPatrolStart) / (kTitleHeroPatrolEnd - kTitleHeroPatrolStart), 0.0f, 1.0f);
        constexpr size_t kSegmentCount = kTitleHeroPatrolOffsets.size() - 1;
        const float segmentProgress = patrolT * static_cast<float>(kSegmentCount);
        const size_t segmentIndex = std::min(static_cast<size_t>(segmentProgress), kSegmentCount - 1);
        const float localT = segmentProgress - static_cast<float>(segmentIndex);
        const Vector3& fromOffset = kTitleHeroPatrolOffsets[segmentIndex];
        const Vector3& toOffset = kTitleHeroPatrolOffsets[segmentIndex + 1];
        const Vector3 routeOffset = LerpTitleVector3(fromOffset, toOffset, SmoothStep(localT));

        position.x += routeOffset.x;
        position.z += routeOffset.z;

        const float hopPhase = localT * kTitleIntroPi * 2.0f;
        const float hop = std::abs(std::sin(hopPhase));
        position.y += hop * 0.58f;
        airborneStretch = hop * 0.055f;
        squash = std::pow(1.0f - hop, 6.0f) * 0.14f;

        const float directionX = toOffset.x - fromOffset.x;
        const float directionZ = toOffset.z - fromOffset.z;
        rotation.y += std::atan2(directionX, directionZ);
        rotation.z += std::sin(hopPhase) * 0.072f;
    } else {
        const float settleTime = time - kTitleHeroPatrolEnd;
        const float settleT = std::clamp(settleTime / 1.05f, 0.0f, 1.0f);
        const float settleJump = std::sin(settleT * kTitleIntroPi) * (1.0f - settleT * 0.32f);
        position.y += settleJump * 0.38f;
        airborneStretch = settleJump * 0.035f;
        squash = std::sin(std::clamp((settleTime - 0.76f) / 0.34f, 0.0f, 1.0f) * kTitleIntroPi) * 0.15f;
        rotation.y += std::sin(settleTime * 0.72f) * 0.12f;
        rotation.z += std::sin(settleTime * 2.3f) * 0.026f;
    }

    scale.x *= 1.0f + squash - airborneStretch * 0.30f;
    scale.y *= 1.0f - squash * 0.82f + airborneStretch;
    scale.z *= 1.0f + squash - airborneStretch * 0.30f;

    titleHeroSlime_->SetIsVisible(true);
    titleHeroSlime_->SetTranslate(position);
    titleHeroSlime_->SetScale(scale);
    titleHeroSlime_->SetRotation(rotation);
    titleHeroSlime_->UpdateLocalMatrix();
    titleHeroSlime_->UpdateWorldMatrix();
}

void TitleScene::UpdateMainMenu() {
    // 枠の編集位置に文字とカーソルを追従させ、選択時だけ強調する。
    InputManager* input = InputManager::GetInstance();

    if (input->IsKeyTriggered(DIK_UP) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP) || input->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_UP)) {
        currentMenuIndex_--;
        if (currentMenuIndex_ < 0)
            currentMenuIndex_ = (int)MenuIndex::Max - 1;
    }
    if (input->IsKeyTriggered(DIK_DOWN) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN) || input->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_DOWN)) {
        currentMenuIndex_++;
        if (currentMenuIndex_ >= (int)MenuIndex::Max)
            currentMenuIndex_ = 0;
    }

    const float pulse = 0.5f + 0.5f * std::sin(titleUiTime_ * 6.0f);
    const float selectedScale = 1.0f + pulse * 0.018f;
    const float menuAlpha =
        std::clamp((titleIntroTime_ - kTitleIntroMenuDelay) / kTitleIntroMenuFadeDuration, 0.0f, 1.0f);
    Vector4 normalColor = {0.24f, 0.22f, 0.17f, 0.98f * menuAlpha};
    Vector4 selectColor = {0.07f, 0.20f, 0.25f, menuAlpha};
    const Vector2 startFramePosition =
        mainMenuStartFrameSprite_ ? mainMenuStartFrameSprite_->GetPosition() : kMenuStartFramePosition;
    const Vector2 settingFramePosition =
        mainMenuSettingFrameSprite_ ? mainMenuSettingFrameSprite_->GetPosition() : kMenuSettingFramePosition;

    if (startTextSprite_) {
        const bool selected = currentMenuIndex_ == (int)MenuIndex::GameStart;
        // エディタでボタンを移動しても文字は常に枠の中心へ追従する。
        startTextSprite_->SetPosition(startFramePosition);
        startTextSprite_->SetColor(selected ? selectColor : normalColor);
        startTextSprite_->SetSize({startTextBaseSize_.x * (selected ? selectedScale : 0.98f),
                                   startTextBaseSize_.y * (selected ? selectedScale : 0.98f)});
    }
    if (settingTextSprite_) {
        const bool selected = currentMenuIndex_ == (int)MenuIndex::Setting;
        settingTextSprite_->SetPosition(settingFramePosition);
        settingTextSprite_->SetColor(selected ? selectColor : normalColor);
        settingTextSprite_->SetSize({settingTextBaseSize_.x * (selected ? selectedScale : 0.98f),
                                     settingTextBaseSize_.y * (selected ? selectedScale : 0.98f)});
    }
    const bool startSelected = currentMenuIndex_ == (int)MenuIndex::GameStart;
    const uint32_t selectedFrame = Sprite::LoadTexture("ui/title/title_menu_button_blue.png");
    const uint32_t normalFrame = Sprite::LoadTexture("ui/title/title_menu_button_beige.png");
    if (mainMenuStartFrameSprite_) {
        mainMenuStartFrameSprite_->SetTextureHandle(startSelected ? selectedFrame : normalFrame);
        mainMenuStartFrameSprite_->SetColor({1.0f, 1.0f, 1.0f, (startSelected ? 1.0f : 0.98f) * menuAlpha});
        mainMenuStartFrameSprite_->Update();
    }
    if (mainMenuSettingFrameSprite_) {
        mainMenuSettingFrameSprite_->SetTextureHandle(startSelected ? normalFrame : selectedFrame);
        mainMenuSettingFrameSprite_->SetColor({1.0f, 1.0f, 1.0f, (startSelected ? 0.98f : 1.0f) * menuAlpha});
        mainMenuSettingFrameSprite_->Update();
    }
    const Vector2 selectedFramePosition = startSelected ? startFramePosition : settingFramePosition;
    if (mainMenuCursorSprite_) {
        const float bob = std::sin(titleUiTime_ * 7.5f) * 5.0f;
        const float squash = 1.0f + pulse * 0.06f;
        mainMenuCursorSprite_->SetPosition({selectedFramePosition.x + mainMenuCursorOffset_.x,
                                            selectedFramePosition.y + mainMenuCursorOffset_.y + bob});
        mainMenuCursorSprite_->SetSize(
            {mainMenuCursorBaseSize_.x * squash, mainMenuCursorBaseSize_.y * (1.05f - pulse * 0.05f)});
        mainMenuCursorSprite_->SetColor({1.0f, 1.0f, 1.0f, menuAlpha});
        mainMenuCursorSprite_->Update();
    }
    if (mainMenuArrowSprite_) {
        const float arrowPulse = 1.0f + pulse * 0.06f;
        mainMenuArrowSprite_->SetPosition(
            {selectedFramePosition.x + mainMenuArrowOffset_.x + std::sin(titleUiTime_ * 8.0f) * 3.0f,
             selectedFramePosition.y + mainMenuArrowOffset_.y});
        mainMenuArrowSprite_->SetSize({mainMenuArrowBaseSize_.x * arrowPulse, mainMenuArrowBaseSize_.y * arrowPulse});
        mainMenuArrowSprite_->SetColor({1.0f, 1.0f, 1.0f, menuAlpha});
        mainMenuArrowSprite_->Update();
    }
    if (mainMenuPromptSprite_) {
        const float promptPulse = 0.5f + 0.5f * std::sin(titleUiTime_ * 1.45f);
        const float promptAlpha = (0.38f + promptPulse * 0.55f) * menuAlpha;
        mainMenuPromptSprite_->SetPosition(mainMenuPromptBasePosition_);
        mainMenuPromptSprite_->SetSize(mainMenuPromptBaseSize_);
        mainMenuPromptSprite_->SetColor({1.0f, 1.0f, 1.0f, promptAlpha});
        mainMenuPromptSprite_->Update();
    }

    if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
        if (currentMenuIndex_ == (int)MenuIndex::GameStart) {
            titleMode_ = TitleMode::SaveSelect;
            saveSelectMode_ = SaveSelectMode::Browse;
            deleteConfirmIndex_ = 1;
            currentSaveSlotIndex_ = std::clamp(currentSaveSlotIndex_, 0, GameDataManager::kSaveSlotCount - 1);
            saveSelectFocusIndex_ = currentSaveSlotIndex_;
            saveSelectOpenTime_ = 0.0f;
            saveLaunchTime_ = 0.0f;
            saveLaunchTransitionRequested_ = false;
            saveSelectionGlowY_ = saveSlotCardBasePositions_[currentSaveSlotIndex_].y;
            saveSlotSelectionWeights_.fill(0.0f);
            DebugConsole::GetInstance()->AddLog("[Title] Open save slot select.");
        } else if (currentMenuIndex_ == (int)MenuIndex::Setting) {
            DebugConsole::GetInstance()->AddLog("[Title] Open settings overlay.");
            if (settingsOverlay_) {
                settingsOverlay_->SetActive(true);
            }
        }
    }
}

void TitleScene::UpdateSaveSelect(float deltaTime) {
    // 削除確認と起動演出中は通常の枠移動を受け付けない。
    InputManager* input = InputManager::GetInstance();

    if (saveSelectMode_ == SaveSelectMode::Launching) {
        // 決定後の演出中は選択を固定し、既存のシーン遷移を一度だけ要求する。
        saveLaunchTime_ += std::max(deltaTime, 0.0f);
        if (saveLaunchTime_ >= kSaveLaunchDuration && !saveLaunchTransitionRequested_) {
            saveLaunchTransitionRequested_ = true;
            StartSelectedSaveSlot();
        }
        return;
    }

    const bool moveUp = input->IsKeyTriggered(DIK_UP) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP) || input->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_UP);
    const bool moveDown = input->IsKeyTriggered(DIK_DOWN) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN) || input->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_DOWN);
    const bool moveLeft = input->IsKeyTriggered(DIK_LEFT) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT) || input->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_LEFT);
    const bool moveRight =
        input->IsKeyTriggered(DIK_RIGHT) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT) || input->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_RIGHT);

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

    if (moveUp) {
        saveSelectFocusIndex_--;
        if (saveSelectFocusIndex_ < 0) {
            saveSelectFocusIndex_ = GameDataManager::kSaveSlotCount - 1;
        }
    }

    if (moveDown) {
        saveSelectFocusIndex_++;
        if (saveSelectFocusIndex_ >= GameDataManager::kSaveSlotCount) {
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

    const GameDataManager::SaveSlotSummary summary =
        GameDataManager::GetInstance()->GetSlotSummary(currentSaveSlotIndex_);
    const bool requestDelete =
        (input->IsKeyTriggered(DIK_D) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_X)) && summary.exists;

    if (requestDelete) {
        saveSelectMode_ = SaveSelectMode::DeleteConfirm;
        deleteConfirmIndex_ = 1;
        DebugConsole::GetInstance()->AddLog("[Title] Open save delete confirm.");
        return;
    }

    if (saveSelectFocusIndex_ < GameDataManager::kSaveSlotCount &&
        (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A))) {
        saveSelectMode_ = SaveSelectMode::Launching;
        saveLaunchTime_ = 0.0f;
        saveLaunchTransitionRequested_ = false;
        DebugConsole::GetInstance()->AddLog("[Title] Confirm save slot.");
    }
}

void TitleScene::InitializeSaveSlotUI() {
    // セーブ枠の部品を配置 JSON の名前から結び、未配置分だけ補う。
    titleUiSprites_.clear();
    saveSlotCards_.fill(nullptr);
    for (auto& frame : saveSlotFrames_) {
        frame.fill(nullptr);
    }
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
    saveSelectBackdrop_ = nullptr;
    saveDetailPanel_ = nullptr;
    saveDetailFileName_ = nullptr;
    saveDetailEmptyLabel_ = nullptr;
    saveLaunchLabel_ = nullptr;
    saveSelectTitle_ = nullptr;
    saveSelectionGlow_ = nullptr;
    saveHintBackdrop_ = nullptr;
    saveKeyboardHint_ = nullptr;
    saveGamepadHint_ = nullptr;
    saveDeleteButtonBack_ = nullptr;
    saveDeleteButtonText_ = nullptr;
    savePromptBubble_ = nullptr;
    saveDeleteQuestionText_ = nullptr;
    saveConfirmYesText_ = nullptr;
    saveConfirmBackText_ = nullptr;

    const float screenW = static_cast<float>(WinApp::kClientWidth);
    const float screenH = static_cast<float>(WinApp::kClientHeight);
    const float centerX = screenW * 0.50f;
    const float firstY = screenH * 0.21f;
    const float slotStep = 172.0f;

    auto bindSprite = [&](Sprite*& target, const std::string& name, const std::string& texture, const Vector2& position,
                          const Vector2& size, const Vector4& color) {
        target = GetSpriteByName(name);
        if (!target) {
            const std::string fullTexture =
                texture.rfind("Resources/", 0) == 0 ? texture : "Resources/sprite/" + texture;
            target = CreateUISprite(fullTexture, position, size, color);
            target->SetName(name);
            target->SetTextureName(texture);
        }
        if (texture.find("generated/text/") != std::string::npos) {
            SetFullSaveUiTexture(target, target->GetTextureHandle(), true);
        }
        target->SetVisible(false);
    };

    saveSelectHeader_ = GetSpriteByName("saveSelectHeaderLine");
    if (saveSelectHeader_) {
        saveSelectHeader_->SetVisible(false);
    }

    bindSprite(saveSelectBackdrop_, "saveSelectBackdrop", kWhite, {screenW * 0.5f, screenH * 0.5f}, {screenW, screenH},
               {0.04f, 0.08f, 0.13f, 0.42f});
    bindSprite(saveDetailPanel_, "saveDetailPanel", "ui/title/save_detail_panel.png", {1375.0f, 475.0f},
               {900.0f, 550.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
    bindSprite(saveDetailFileName_, "saveDetailFileName", kTextFile1, {1430.0f, 290.0f}, {290.0f, 70.0f},
               {0.20f, 0.26f, 0.38f, 1.0f});
    bindSprite(saveDetailEmptyLabel_, "saveDetailEmptyLabel", "ui/title/save_beginning_label.png",
               {1375.0f, 550.0f}, {310.0f, 64.0f}, {0.20f, 0.32f, 0.48f, 1.0f});
    bindSprite(saveLaunchLabel_, "saveLaunchLabel", "ui/title/save_launch_label.png",
               {1375.0f, 672.0f}, {340.0f, 70.0f}, {0.17f, 0.39f, 0.65f, 1.0f});
    bindSprite(saveSelectTitle_, "saveSelectTitle", "ui/title/save_select_title.png", {960.0f, 120.0f},
               {670.0f, 95.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
    bindSprite(saveSelectionGlow_, "saveSelectionGlow", kSaveSlotCardSelected, {495.0f, 320.0f},
               {790.0f, 160.0f}, {1.0f, 1.0f, 1.0f, 0.35f});
    bindSprite(saveHintBackdrop_, "saveHintBackdrop", kWhite, {960.0f, 965.0f},
               {1250.0f, 82.0f}, {0.02f, 0.07f, 0.14f, 0.68f});
    bindSprite(saveKeyboardHint_, "saveKeyboardHint", "ui/title/save_hint_keyboard.png", {960.0f, 970.0f},
               {1120.0f, 54.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
    bindSprite(saveGamepadHint_, "saveGamepadHint", "ui/title/save_hint_gamepad.png", {960.0f, 970.0f},
               {1120.0f, 54.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
    saveDetailPanelBasePosition_ = saveDetailPanel_->GetPosition();
    saveSelectTitleBasePosition_ = saveSelectTitle_->GetPosition();

    for (int i = 0; i < GameDataManager::kSaveSlotCount; ++i) {
        const Vector2 pos = {centerX, firstY + slotStep * static_cast<float>(i)};
        const std::string suffix = "_" + std::to_string(i);

        bindSprite(saveSlotCards_[i], "saveSlotCard" + suffix, kSaveSlotCard, pos, {1120.0f, 162.0f},
                   {1.0f, 1.0f, 1.0f, 0.92f});
        saveSlotCardBaseSizes_[i] = saveSlotCards_[i]->GetSize();
        saveSlotCardBasePositions_[i] = saveSlotCards_[i]->GetPosition();
        bindSprite(saveSlotNumberSprites_[i], "saveSlotNumber" + suffix, "number/big1.png",
                   {pos.x - 470.0f, pos.y - 2.0f}, {72.0f, 106.0f}, {1.0f, 0.98f, 0.78f, 1.0f});
        saveSlotNumberBasePositions_[i] = saveSlotNumberSprites_[i]->GetPosition();
        bindSprite(saveSlotFileNameTexts_[i], "saveSlotFileName" + suffix, kFileTextPaths[i],
                   {pos.x - 260.0f, pos.y - 43.0f}, {226.0f, 87.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
        saveSlotFileNameBasePositions_[i] = saveSlotFileNameTexts_[i]->GetPosition();
        bindSprite(saveSlotStatusTexts_[i], "saveSlotStatus" + suffix, kTextStartFromBeginning,
                   {pos.x - 260.0f, pos.y + 38.0f}, {210.0f, 92.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
        saveSlotStatusBasePositions_[i] = saveSlotStatusTexts_[i]->GetPosition();
        bindSprite(saveSlotCrownIcons_[i], "saveSlotCrownIcon" + suffix, kCrownIcon, {pos.x + 65.0f, pos.y - 38.0f},
                   {58.0f, 58.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
        bindSprite(saveSlotCrownXIcons_[i], "saveSlotCrownX" + suffix, kXIcon, {pos.x + 114.0f, pos.y - 38.0f},
                   {36.0f, 36.0f}, {1.0f, 0.98f, 0.70f, 1.0f});
        for (int digit = 0; digit < 2; ++digit) {
            bindSprite(saveSlotCrownDigits_[i][digit], "saveSlotCrownDigit" + std::to_string(digit) + suffix,
                       "number/0.png", {pos.x + 153.0f + digit * 31.0f, pos.y - 38.0f}, {28.0f, 42.0f},
                       {1.0f, 0.95f, 0.55f, 1.0f});
        }

        bindSprite(saveSlotStarIcons_[i], "saveSlotStarIcon" + suffix, kStarIcon, {pos.x + 65.0f, pos.y + 28.0f},
                   {54.0f, 54.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
        bindSprite(saveSlotStarXIcons_[i], "saveSlotStarX" + suffix, kXIcon, {pos.x + 114.0f, pos.y + 28.0f},
                   {36.0f, 36.0f}, {1.0f, 0.98f, 0.70f, 1.0f});
        for (int digit = 0; digit < 2; ++digit) {
            bindSprite(saveSlotStarDigits_[i][digit], "saveSlotStarDigit" + std::to_string(digit) + suffix,
                       "number/0.png", {pos.x + 153.0f + digit * 31.0f, pos.y + 28.0f}, {28.0f, 42.0f},
                       {1.0f, 0.95f, 0.55f, 1.0f});
        }

        bindSprite(saveSlotLifeIcons_[i], "saveSlotLifeIcon" + suffix, kLivesLabel, {pos.x + 355.0f, pos.y + 28.0f},
                   {110.0f, 48.0f}, {1.0f, 1.0f, 1.0f, 0.95f});
        bindSprite(saveSlotLifeXIcons_[i], "saveSlotLifeX" + suffix, kXIcon, {pos.x + 404.0f, pos.y + 28.0f},
                   {36.0f, 36.0f}, {1.0f, 0.95f, 0.58f, 0.95f});
        for (int digit = 0; digit < 2; ++digit) {
            bindSprite(saveSlotLifeDigits_[i][digit], "saveSlotLifeDigit" + std::to_string(digit) + suffix,
                       "number/0.png", {pos.x + 443.0f + digit * 31.0f, pos.y + 28.0f}, {28.0f, 42.0f},
                       {1.0f, 0.94f, 0.56f, 1.0f});
        }

        bindSprite(saveSlotPlayTimeLabels_[i], "saveSlotPlayTimeLabel" + suffix, kTextPlayTime,
                   {pos.x + 325.0f, pos.y - 38.0f}, {150.0f, 54.0f}, {1.0f, 1.0f, 1.0f, 0.86f});
        for (int digit = 0; digit < 5; ++digit) {
            const char* texture = digit == 2 ? "number/colon.png" : "number/0.png";
            bindSprite(saveSlotPlayTimeDigits_[i][digit], "saveSlotPlayTimeDigit" + std::to_string(digit) + suffix,
                       texture, {pos.x + 418.0f + digit * 24.0f, pos.y - 38.0f},
                       digit == 2 ? Vector2{13.0f, 38.0f} : Vector2{21.0f, 38.0f}, {1.0f, 0.95f, 0.70f, 0.95f});
        }
    }

    bindSprite(saveDeleteButtonBack_, "saveDeleteButtonBack", kSaveDeleteButton, {screenW * 0.25f, screenH * 0.82f},
               {430.0f, 86.0f}, {1.0f, 1.0f, 1.0f, 0.88f});
    bindSprite(saveDeleteButtonText_, "saveDeleteButtonText", kTextDeleteFile, {screenW * 0.25f, screenH * 0.82f},
               {278.0f, 74.0f}, {1.0f, 1.0f, 1.0f, 0.96f});
    bindSprite(savePromptBubble_, "savePromptBubble", "ui/title/save_prompt_bubble.png",
               {screenW * 0.70f, screenH * 0.82f}, {720.0f, 190.0f}, {1.0f, 1.0f, 1.0f, 0.92f});
    bindSprite(saveDeleteQuestionText_, "saveDeleteQuestionText", kTextDeleteQuestion,
               {screenW * 0.70f, screenH * 0.76f}, {300.0f, 92.0f}, {0.28f, 0.20f, 0.14f, 1.0f});
    bindSprite(saveConfirmYesText_, "saveConfirmYesText", kTextYes, {screenW * 0.64f, screenH * 0.85f}, {92.0f, 72.0f},
               {0.28f, 0.20f, 0.14f, 1.0f});
    bindSprite(saveConfirmBackText_, "saveConfirmBackText", kTextBack, {screenW * 0.77f, screenH * 0.85f},
               {132.0f, 74.0f}, {0.28f, 0.20f, 0.14f, 1.0f});
}

void TitleScene::SetNumberSprites(std::array<Sprite*, 2>& digits, int value, const Vector4& color, bool visible) {
    // 保存値を二桁表示へ丸め、桁ごとの Sprite を再利用する。
    value = std::clamp(value, 0, 99);

    std::array<int, 2> digitValues = {value / 10, value % 10};
    const int digitCount = value >= 10 ? 2 : 1;

    for (int i = 0; i < 2; ++i) {
        Sprite* sprite = digits[i];
        if (!sprite)
            continue;

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

Sprite* TitleScene::CreateUISprite(const std::string& texturePath, const Vector2& position, const Vector2& size,
                                   const Vector4& color) {
    // 実行時に補う UI も配置済み Sprite と同じ所有コンテナへ登録する。
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteCommon_.get(), texturePath);
    sprite->SetPosition(position);
    sprite->SetSize(size);
    sprite->SetColor(color);
    sprite->SetAnchorPoint({0.5f, 0.5f});
    sprite->SetVisible(false);
    sprite->Update();

    Sprite* raw = sprite.get();
    titleUiSprites_.push_back(std::move(sprite));
    return raw;
}

void TitleScene::UpdateSaveSlotUI(float deltaTime) {
    // セーブ記録と選択状態を基準配置へ毎フレーム反映し、画面の再開閉でもずれを残さない。
    // 入場の時間差と起動中の強調を先に数値化し、個々のSpriteへ同じ進捗を配る。
    const bool inSaveSelect = titleMode_ == TitleMode::SaveSelect;
    const bool deleteConfirm = inSaveSelect && saveSelectMode_ == SaveSelectMode::DeleteConfirm;
    const bool launching = inSaveSelect && saveSelectMode_ == SaveSelectMode::Launching;
    const float launchFocus = SmoothStep(std::clamp(saveLaunchTime_ / 0.32f, 0.0f, 1.0f));
    const float launchBounce = launching ? std::sin(kTitleIntroPi * std::clamp(saveLaunchTime_ / 0.48f, 0.0f, 1.0f)) : 0.0f;
    saveSelectOpenTime_ = inSaveSelect ? saveSelectOpenTime_ + std::max(deltaTime, 0.0f) : 0.0f;
    const float titleReveal = SmoothStep(std::clamp(saveSelectOpenTime_ / 0.25f, 0.0f, 1.0f));
    const float panelReveal = SmoothStep(std::clamp((saveSelectOpenTime_ - 0.12f) / 0.28f, 0.0f, 1.0f));
    const float hintReveal = SmoothStep(std::clamp((saveSelectOpenTime_ - 0.22f) / 0.25f, 0.0f, 1.0f));
    const bool settingsOpen = settingsOverlay_ && settingsOverlay_->IsActive();
    const bool showMainMenu = !settingsOpen && !inSaveSelect && titleIntroComplete_;
    // メインメニューとセーブ選択画面を同時に見せないよう、共通部品の可視性を揃える。
    if (titleTextSprite_)
        titleTextSprite_->SetVisible(false);
    if (startTextSprite_)
        startTextSprite_->SetVisible(showMainMenu);
    if (settingTextSprite_)
        settingTextSprite_->SetVisible(showMainMenu);
    if (mainMenuStartFrameSprite_)
        mainMenuStartFrameSprite_->SetVisible(showMainMenu);
    if (mainMenuSettingFrameSprite_)
        mainMenuSettingFrameSprite_->SetVisible(showMainMenu);
    if (mainMenuArrowSprite_)
        mainMenuArrowSprite_->SetVisible(showMainMenu);
    if (mainMenuCursorSprite_)
        mainMenuCursorSprite_->SetVisible(showMainMenu);
    if (mainMenuPromptSprite_)
        mainMenuPromptSprite_->SetVisible(showMainMenu);
    if (saveSelectHeader_)
        saveSelectHeader_->SetVisible(false);

    const float pulse = 0.5f + 0.5f * std::sin(titleUiTime_ * 5.0f);

    // 暗幕は削除確認で濃くし、起動中は選択カードを目立たせる。
    if (saveSelectBackdrop_) {
        saveSelectBackdrop_->SetVisible(inSaveSelect);
        saveSelectBackdrop_->SetColor(deleteConfirm ? Vector4{0.03f, 0.05f, 0.08f, 0.82f}
                                    : launching ? Vector4{0.04f, 0.08f, 0.13f, 0.58f + 0.08f * launchFocus}
                                                : Vector4{0.04f, 0.08f, 0.13f, 0.58f});
    }

    // 詳細パネル、タイトル、操作ガイドは異なる遅延で現れる。
    const bool gamepadHints = InputManager::GetInstance()->IsGamepadMode();
    if (saveDetailPanel_) {
        saveDetailPanel_->SetVisible(inSaveSelect && panelReveal > 0.0f);
        saveDetailPanel_->SetPosition({saveDetailPanelBasePosition_.x,
                                       saveDetailPanelBasePosition_.y + 24.0f * (1.0f - panelReveal)});
        saveDetailPanel_->SetColor({1.0f, 1.0f, 1.0f, panelReveal});
    }
    if (saveSelectTitle_) {
        saveSelectTitle_->SetVisible(inSaveSelect && titleReveal > 0.0f);
        saveSelectTitle_->SetPosition({saveSelectTitleBasePosition_.x,
                                       saveSelectTitleBasePosition_.y - 18.0f * (1.0f - titleReveal)});
        saveSelectTitle_->SetColor({1.0f, 1.0f, 1.0f, titleReveal});
    }
    if (saveHintBackdrop_) {
        saveHintBackdrop_->SetVisible(inSaveSelect && !deleteConfirm && !launching && hintReveal > 0.0f);
        saveHintBackdrop_->SetColor({0.02f, 0.07f, 0.14f, 0.68f * hintReveal});
    }
    if (saveKeyboardHint_) {
        saveKeyboardHint_->SetVisible(inSaveSelect && !gamepadHints && !deleteConfirm && !launching && hintReveal > 0.0f);
        saveKeyboardHint_->SetColor({1.0f, 1.0f, 1.0f, hintReveal});
    }
    if (saveGamepadHint_) {
        saveGamepadHint_->SetVisible(inSaveSelect && gamepadHints && !deleteConfirm && !launching && hintReveal > 0.0f);
        saveGamepadHint_->SetColor({1.0f, 1.0f, 1.0f, hintReveal});
    }
    // 選択中スロットのファイル番号と空欄表示は保存要約から毎フレーム更新する。
    if (saveDetailFileName_) {
        saveDetailFileName_->SetVisible(inSaveSelect && panelReveal > 0.0f);
        saveDetailFileName_->SetColor({0.20f, 0.26f, 0.38f, panelReveal});
        const std::string fileTexture = kFileTextPaths[currentSaveSlotIndex_];
        SetFullSaveUiTexture(saveDetailFileName_,
                             Sprite::LoadTexture(fileTexture.substr(std::string("Resources/sprite/").size())), true);
    }
    if (saveDetailEmptyLabel_) {
        const auto summary = GameDataManager::GetInstance()->GetSlotSummary(currentSaveSlotIndex_);
        saveDetailEmptyLabel_->SetVisible(inSaveSelect && !summary.exists && panelReveal > 0.0f);
        saveDetailEmptyLabel_->SetColor({0.20f, 0.32f, 0.48f, panelReveal});
    }
    if (saveLaunchLabel_) {
        const float reveal = SmoothStep(std::clamp((saveLaunchTime_ - 0.08f) / 0.24f, 0.0f, 1.0f));
        saveLaunchLabel_->SetVisible(launching && reveal > 0.0f);
        saveLaunchLabel_->SetColor({0.17f, 0.39f, 0.65f, reveal});
    }

    // 各行の登場時間、選択強調、起動時の減光を計算してからカードと文字へ反映する。
    for (int i = 0; i < GameDataManager::kSaveSlotCount; ++i) {
        const float rowReveal = SmoothStep(std::clamp((saveSelectOpenTime_ - i * 0.065f) / 0.26f, 0.0f, 1.0f));
        const float selectionTarget = inSaveSelect && !deleteConfirm && saveSelectFocusIndex_ == i ? 1.0f : 0.0f;
        saveSlotSelectionWeights_[i] += (selectionTarget - saveSlotSelectionWeights_[i]) *
                                        std::clamp(deltaTime * 12.0f, 0.0f, 1.0f);
        const bool selected = inSaveSelect && !deleteConfirm && saveSelectFocusIndex_ == i;
        const float launchDim = launching && !selected ? 1.0f - 0.58f * launchFocus : 1.0f;
        const float launchLift = launching && selected ? -9.0f * launchFocus : 0.0f;
        const float launchScale = launching && selected ? launchBounce : 0.0f;
        const bool deleteTarget = inSaveSelect && deleteConfirm && i == currentSaveSlotIndex_;
        const GameDataManager::SaveSlotSummary summary = GameDataManager::GetInstance()->GetSlotSummary(i);
        const bool emphasized = selected || deleteTarget;
        const Vector4 cardColor = emphasized ? Vector4{1.0f, 1.0f, 1.0f, 0.98f + pulse * 0.02f}
                                             : Vector4{1.0f, 1.0f, 1.0f, summary.exists ? 0.98f : 1.0f};
        const Vector4 iconColor = summary.exists ? Vector4{1.0f, 1.0f, 1.0f, emphasized ? 1.0f : 0.96f}
                                                 : Vector4{0.95f, 0.95f, 0.95f, emphasized ? 0.86f : 0.72f};
        // 淡いカード上では明るい黄色の数字が埋もれるため、選択状態ごとに濃い色を使う。
        const Vector4 numberColor = emphasized ? Vector4{0.48f, 0.24f, 0.08f, 1.0f}
                                               : Vector4{0.20f, 0.34f, 0.52f, summary.exists ? 1.0f : 0.82f};
        const Vector4 textColor = emphasized ? Vector4{0.32f, 0.16f, 0.20f, 1.0f} : Vector4{0.16f, 0.25f, 0.38f, 1.0f};

        if (saveSlotCards_[i]) {
            saveSlotCards_[i]->SetVisible(inSaveSelect && rowReveal > 0.0f);
            const char* cardTexture =
                emphasized ? kSaveSlotCardSelected : (summary.exists ? kSaveSlotCard : kSaveSlotCardEmpty);
            SetFullSaveUiTexture(saveSlotCards_[i], Sprite::LoadTexture(cardTexture));
            // 選択枠だけ左右へ広げる。基準値は配置JSONに残す。
            const Vector2 baseSize = saveSlotCardBaseSizes_[i];
            saveSlotCards_[i]->SetSize({baseSize.x + 24.0f * saveSlotSelectionWeights_[i] + 18.0f * launchScale,
                                       baseSize.y + 8.0f * launchScale});
            // JSON の基準位置から毎フレーム再計算し、開閉を繰り返しても座標がずれないようにする。
            saveSlotCards_[i]->SetPosition({saveSlotCardBasePositions_[i].x,
                                            saveSlotCardBasePositions_[i].y + 25.0f * (1.0f - rowReveal) + launchLift});
            saveSlotCards_[i]->SetColor({cardColor.x, cardColor.y, cardColor.z, cardColor.w * rowReveal * launchDim});
        }

        // 古いフレーム部品は表示せず、カード画像を選択状態に合わせて差し替える。
        for (auto* frame : saveSlotFrames_[i]) {
            if (frame) {
                frame->SetVisible(false);
            }
        }

        if (saveSlotNumberSprites_[i]) {
            const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(i + 1) + ".png");
            saveSlotNumberSprites_[i]->SetTextureHandle(handle);
            saveSlotNumberSprites_[i]->SetVisible(inSaveSelect && rowReveal > 0.0f);
            saveSlotNumberSprites_[i]->SetPosition({saveSlotNumberBasePositions_[i].x,
                                                    saveSlotNumberBasePositions_[i].y + 25.0f * (1.0f - rowReveal) + launchLift});
            saveSlotNumberSprites_[i]->SetColor({numberColor.x, numberColor.y, numberColor.z,
                                                  numberColor.w * rowReveal * launchDim});
        }

        if (saveSlotFileNameTexts_[i]) {
            saveSlotFileNameTexts_[i]->SetVisible(inSaveSelect && rowReveal > 0.0f);
            saveSlotFileNameTexts_[i]->SetPosition({saveSlotFileNameBasePositions_[i].x,
                                                   saveSlotFileNameBasePositions_[i].y + 25.0f * (1.0f - rowReveal) + launchLift});
            saveSlotFileNameTexts_[i]->SetColor({textColor.x, textColor.y, textColor.z,
                                                 textColor.w * rowReveal * launchDim});
        }

        if (saveSlotStatusTexts_[i]) {
            // チュートリアル未クリアのファイルだけ開始状態を示す。クリア後はファイル名を主役にする。
            const std::string statusTexture = kTextStartFromBeginning;
            SetFullSaveUiTexture(saveSlotStatusTexts_[i],
                                 Sprite::LoadTexture(statusTexture.substr(std::string("Resources/sprite/").size())));
            saveSlotStatusTexts_[i]->SetVisible(inSaveSelect && !summary.tutorialCleared && rowReveal > 0.0f);
            saveSlotStatusTexts_[i]->SetPosition({saveSlotStatusBasePositions_[i].x,
                                                 saveSlotStatusBasePositions_[i].y + 25.0f * (1.0f - rowReveal) + launchLift});
            saveSlotStatusTexts_[i]->SetColor({textColor.x, textColor.y, textColor.z,
                                               textColor.w * rowReveal * launchDim});
        }

        // 詳細欄は実データがある選択行だけに出し、王冠・スター・残機を同じ透明度で見せる。
        const bool showDetails = inSaveSelect && panelReveal > 0.0f && summary.exists && i == currentSaveSlotIndex_;
        const Vector4 detailIconColor = {iconColor.x, iconColor.y, iconColor.z, iconColor.w * panelReveal};
        const Vector4 detailNumberColor = {numberColor.x, numberColor.y, numberColor.z, numberColor.w * panelReveal};
        const Vector4 detailTextColor = {textColor.x, textColor.y, textColor.z, textColor.w * panelReveal};
        if (saveSlotCrownIcons_[i]) {
            saveSlotCrownIcons_[i]->SetVisible(showDetails);
            saveSlotCrownIcons_[i]->SetColor(detailIconColor);
        }
        if (saveSlotCrownXIcons_[i]) {
            saveSlotCrownXIcons_[i]->SetVisible(showDetails);
            saveSlotCrownXIcons_[i]->SetColor(detailNumberColor);
        }
        SetNumberSprites(saveSlotCrownDigits_[i], summary.clearedStageCount, detailNumberColor, showDetails);

        if (saveSlotStarIcons_[i]) {
            saveSlotStarIcons_[i]->SetVisible(showDetails);
            saveSlotStarIcons_[i]->SetColor(detailIconColor);
        }
        if (saveSlotStarXIcons_[i]) {
            saveSlotStarXIcons_[i]->SetVisible(showDetails);
            saveSlotStarXIcons_[i]->SetColor(detailNumberColor);
        }
        SetNumberSprites(saveSlotStarDigits_[i], summary.collectedStarCoins, detailNumberColor, showDetails);

        if (saveSlotLifeIcons_[i]) {
            saveSlotLifeIcons_[i]->SetVisible(showDetails);
            saveSlotLifeIcons_[i]->SetColor(detailIconColor);
        }
        if (saveSlotLifeXIcons_[i]) {
            saveSlotLifeXIcons_[i]->SetVisible(showDetails);
            saveSlotLifeXIcons_[i]->SetColor(detailNumberColor);
        }

        SetNumberSprites(saveSlotLifeDigits_[i], summary.lives, detailNumberColor, showDetails);

        if (saveSlotPlayTimeLabels_[i]) {
            saveSlotPlayTimeLabels_[i]->SetVisible(showDetails);
            saveSlotPlayTimeLabels_[i]->SetColor(detailTextColor);
        }

        // 既存の5枚の数字Spriteを時:分として使い、長時間プレイでも値を止めない。
        const int totalMinutes = std::clamp(summary.playTimeSeconds / 60, 0, 99 * 60 + 59);
        const int hours = totalMinutes / 60;
        const int minutes = totalMinutes % 60;
        const std::array<int, 4> timeDigits = {hours / 10, hours % 10, minutes / 10, minutes % 10};
        int digitIndex = 0;
        for (int part = 0; part < 5; ++part) {
            Sprite* sprite = saveSlotPlayTimeDigits_[i][part];
            if (!sprite)
                continue;

            const bool visible = showDetails;
            sprite->SetVisible(visible);
            if (!visible)
                continue;

            if (part == 2) {
                sprite->SetTextureHandle(Sprite::LoadTexture("number/colon.png"));
            } else {
                const int value = timeDigits[digitIndex++];
                sprite->SetTextureHandle(Sprite::LoadTexture("number/" + std::to_string(value) + ".png"));
            }
            sprite->SetColor(detailNumberColor);
        }
    }

    // 選択枠はキー入力で行が変わっても瞬間移動させず、現在の行へ滑らかに追従する。
    const int focusedRow = std::clamp(saveSelectFocusIndex_, 0, GameDataManager::kSaveSlotCount - 1);
    const float follow = std::clamp(deltaTime * 12.0f, 0.0f, 1.0f);
    saveSelectionGlowY_ += (saveSlotCardBasePositions_[focusedRow].y - saveSelectionGlowY_) * follow;
    if (saveSelectionGlow_) {
        const float reveal = SmoothStep(std::clamp((saveSelectOpenTime_ - focusedRow * 0.065f) / 0.26f, 0.0f, 1.0f));
        saveSelectionGlow_->SetVisible(inSaveSelect && !deleteConfirm && reveal > 0.0f);
        saveSelectionGlow_->SetPosition({saveSlotCardBasePositions_[focusedRow].x,
                                         saveSelectionGlowY_ - (launching ? 9.0f * launchFocus : 0.0f)});
        saveSelectionGlow_->SetSize({saveSlotCardBaseSizes_[focusedRow].x + 42.0f + 24.0f * launchBounce,
                                     saveSlotCardBaseSizes_[focusedRow].y + 20.0f + 12.0f * launchBounce});
        saveSelectionGlow_->SetColor({1.0f, 1.0f, 1.0f, (0.38f + 0.36f * launchBounce) * reveal});
    }

    if (saveDeleteButtonBack_) {
        saveDeleteButtonBack_->SetVisible(false);
    }
    if (saveDeleteButtonText_) {
        saveDeleteButtonText_->SetVisible(false);
    }

    // 削除確認中だけ確認Popupを出し、Yes/Backの選択側を点滅させる。
    if (savePromptBubble_) {
        savePromptBubble_->SetVisible(deleteConfirm);
        savePromptBubble_->SetColor({1.0f, 0.86f, 0.64f, 0.94f});
    }
    if (saveDeleteQuestionText_) {
        saveDeleteQuestionText_->SetVisible(deleteConfirm);
    }
    if (saveConfirmYesText_) {
        saveConfirmYesText_->SetVisible(deleteConfirm);
        saveConfirmYesText_->SetColor(deleteConfirmIndex_ == 0 ? Vector4{0.08f, 0.44f + pulse * 0.18f, 0.96f, 1.0f}
                                                               : Vector4{0.28f, 0.20f, 0.14f, 0.70f});
    }
    if (saveConfirmBackText_) {
        saveConfirmBackText_->SetVisible(deleteConfirm);
        saveConfirmBackText_->SetColor(deleteConfirmIndex_ == 1 ? Vector4{0.08f, 0.44f + pulse * 0.18f, 0.96f, 1.0f}
                                                                : Vector4{0.28f, 0.20f, 0.14f, 0.70f});
    }
}

// 空スロットは初期データで開始します。チュートリアルの完了記録がない場合は選択画面を飛ばします。
void TitleScene::StartSelectedSaveSlot() {
    // 確定した枠を有効化し、チュートリアルのクリア記録で開始シーンを決める。
    GameDataManager* saveData = GameDataManager::GetInstance();
    const GameDataManager::SaveSlotSummary summary = saveData->GetSlotSummary(currentSaveSlotIndex_);

    saveData->SetActiveSlot(currentSaveSlotIndex_);
    if (!summary.exists) {
        saveData->ResetAll();
    }

    const bool tutorialCleared = saveData->IsStageCleared(-1);
    DebugConsole::GetInstance()->AddLog(tutorialCleared ? "[Title] Start from stage select."
                                                        : "[Title] Start tutorial.");
    SceneManager::GetInstance()->ChangeScene(tutorialCleared ? "SELECT" : "TUTORIAL");
}

void TitleScene::DeleteSelectedSaveSlot() {
    // 空枠の削除を避け、実際の削除結果をデバッグログへ残す。
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
    // 暗幕は DrawUI で先に描くため、ここではカードと文字だけを重ねる。
    for (auto& sprite : titleUiSprites_) {
        if (sprite.get() != saveSelectBackdrop_) {
            sprite->Draw();
        }
    }
}

void TitleScene::Draw() {
    // 視点がプレイヤーの内側にある場合は本体を描かず、各パスの順序を維持する。
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

    // 不透明物体を先に描き、透明・霧・特殊材質はそれぞれ後段のパスへ回す。
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_)
            continue;
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || IsSpecialMaterialType(obj->GetMaterialType()))
            continue; // フォグ(7)も不透明パスから除外
        obj->Draw(pointLightRes, spotLightRes);
    }

    // 弾と予告を不透明物体より前面、透明物体より手前の段階で描く。
    DrawEnemyTelegraphs(*this, pointLightRes, spotLightRes);
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    LightEditor::GetInstance()->Draw3D();

    // 透明物体と粒子の後、霧・GrabTextureを使う特殊材質を描画する。
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_)
            continue;
        if (obj->GetMaterialType() == 1) { // 透明のみ描画
            obj->Draw(pointLightRes, spotLightRes);
        }
    }
    particleSystem_->Draw();

    DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);
    const bool grabUpdated =
        DrawSpecialMaterialObjects(objects, dxCommon_, BulletManager::GetInstance(), player_, isFirstPerson);
    DrawGPUParticles(dxCommon_, camera, gpuParticleTexHandle_, grabUpdated);
}

// タイトルとセーブ選択の2D部品を、暗幕から設定画面まで順に重ねる。
void TitleScene::DrawUI() {
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    // 背景用の暗幕は、JSON配置と実行時生成のどちらでもカード・文字より先に一度だけ描画します。
    if (saveSelectBackdrop_) {
        saveSelectBackdrop_->Draw();
    }
    for (auto& sprite : sprites_) {
        if (sprite.get() != saveSelectBackdrop_) {
            sprite->Draw();
        }
    }
    DrawSaveSlotUI();
    if (settingsOverlay_) {
        settingsOverlay_->Draw();
    }
}

// シャドウマップ描画の実装
void TitleScene::DrawShadow() {
    // 背景の 3D 配置を ObjectManager の影描画へ委ねる。
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

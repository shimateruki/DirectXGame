#define NOMINMAX
#include "game/scene/gameover/GameOverScene.h"
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
#include "VFXSequencer.h"
#include "Fade.h"
#include "PostEffect.h"
#include "GameDataManager.h"
#include "WinApp.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kTitleLetterInterval = 0.11f;
constexpr float kTitleLetterPopDuration = 0.28f;
constexpr float kMenuRevealDelay = 0.62f;
constexpr const char* kFallingCrownSpritePrefix = "gameover_falling_crown_";
constexpr const char* kDizzyStarSpritePrefix = "gameover_dizzy_star_";
constexpr const char* kGameOverImpactCue = "game_over_impact_cue";

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

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

float EaseInOutCubic(float t) {
    t = Clamp01(t);
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    const float inv = -2.0f * t + 2.0f;
    return 1.0f - inv * inv * inv * 0.5f;
}

float LerpFloat(float start, float end, float t) { return start + (end - start) * Clamp01(t); }

Vector3 MakeLookAtEuler(const Vector3& eye, const Vector3& target) {
    const Vector3 direction = target - eye;
    const float planarLength = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    return {std::atan2(-direction.y, planarLength), std::atan2(direction.x, direction.z), 0.0f};
}

bool IsFadePlayingForSceneIntro() {
    const Fade::Status status = Fade::GetInstance()->GetStatus();
    return status == Fade::Status::FadeIn || status == Fade::Status::FadeOut || status == Fade::Status::IrisIn ||
           status == Fade::Status::IrisOut;
}
} // namespace

SceneLoadManifest GameOverScene::BuildAsyncLoadManifest() const {
    SceneLoadManifest manifest;
    manifest.AddObjectLayout(HasSceneAssetContext() && !GetSceneLoadContext().objectLayoutPath.empty()
                                 ? GetSceneLoadContext().objectLayoutPath
                                 : "Resources/json/3Dobject/gameOverScene.json");
    manifest.AddSpriteLayout(HasSceneAssetContext() && !GetSceneLoadContext().spriteLayoutPath.empty()
                                 ? GetSceneLoadContext().spriteLayoutPath
                                 : "Resources/json/sprite/gameOverScene.json");
    manifest.AddModel("Characters/player");
    manifest.AddTexture("Resources/sprite/common/white.png");
    return manifest;
}

void GameOverScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();

    ModelManager::GetInstance()->LoadModel("Characters/player");
    LOG("GameOverScene Initialized!");


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
    ParticleManager::GetInstance()->Initialize(particleSystem_.get());

    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    objectManager_ = std::make_unique<ObjectManager>();
    levelLoader_ = std::make_unique<LevelLoader>();
    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);

    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

    levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/gameOverScene.json");
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/gameOverScene.json");
    BindLayoutSprites();
    // 背景にも王冠があるため、前景へ重ねて降らせない。
    InitializeDizzyStars();

    LightManager::GetInstance()->LoadState(ResolveSceneLightPath("Resources/json/light/gameOverScene.json"));
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile(ResolveSceneCameraPath("gameOver_camera.json"));
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
    CameraEditor::GetInstance()->Update(nullptr, false);
    CameraManager::GetInstance()->Update();
}

void GameOverScene::OnActivated() {
    BaseScene::OnActivated();
    sceneTime_ = 0.0f;
    titleRevealTimer_ = 0.0f;
    selectedIndex_ = 0;
    retryExitActive_ = false;
    retrySceneChangeRequested_ = false;
    retryExitTimer_ = 0.0f;
    VFXSequencer::ClearOneShots();
    ResetGameOverPostEffects();
    if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
        params->dangerVignette = 0.0f;
    }
    InitializeGameOverPresentation();
    UpdateGameOverCamera();
    CameraManager::GetInstance()->Update(0.0f);
    UpdateMenuSprites(0.0f);
    for (auto& object : objectManager_->GetObjects())
        object->RefreshRenderCameraData();
    for (auto& sprite : sprites_)
        sprite->Update();
    dxCommon_->SetRenderClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void GameOverScene::Finalize() {
    ResetGameOverPostEffects();
    VFXSequencer::ClearOneShots();
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();

    objectManager_.reset();
    gameOverSlimeObject_ = nullptr;
    gameOverSlimeAnimator_.Reset(nullptr);
    backgroundSprite_ = nullptr;
    titleLetters_.fill(nullptr);
    titleLetterBasePositions_.fill(Vector2{0.0f, 0.0f});
    titleLetterBaseSizes_.fill(Vector2{0.0f, 0.0f});
    menuRows_ = {};
    fallingCrowns_.clear();
    dizzyStars_.clear();
    sprites_.clear();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

// 導入フェード中は演出時計とメニュー入力を止め、シーンの通常更新だけ継続します。
void GameOverScene::Update(float deltaTime) {
    const bool canAdvanceIntro = !IsFadePlayingForSceneIntro();
    const float introDeltaTime = canAdvanceIntro ? deltaTime : 0.0f;
    sceneTime_ += introDeltaTime;
    titleRevealTimer_ = std::max(0.0f, sceneTime_ - presentationTuning_.titleRevealStartTime);

    if (SceneManager::GetInstance()->IsPlaying() && canAdvanceIntro && !retryExitActive_ &&
        !retrySceneChangeRequested_ && IsTitleRevealComplete()) {
        UpdateMenuInput();
    }

    UpdateMenuSprites(introDeltaTime);
    UpdateFallingCrowns(introDeltaTime);

    objectManager_->Update(deltaTime);
    UpdateGameOverPresentation(introDeltaTime);

    LightEditor::GetInstance()->Update();
    CameraEditor::GetInstance()->Update(player_, false);
    UpdateGameOverCamera();
    CameraManager::GetInstance()->Update(deltaTime);
    // カメラ更新後の行列で同一フレームのモデルを描画する。
    for (auto& object : objectManager_->GetObjects())
        object->RefreshRenderCameraData();
    UpdateDizzyStars(introDeltaTime);

    particleSystem_->Update(deltaTime);
    UpdateRetryExit(introDeltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }

    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
}

void GameOverScene::Draw() {
    DrawBackgroundSprite();

    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
#ifndef _DEBUG
    if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_)
            continue;
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || IsSpecialMaterialType(obj->GetMaterialType()))
            continue;
        obj->Draw(pointLightRes, spotLightRes);
    }

    DrawEnemyTelegraphs(pointLightRes, spotLightRes);
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    LightEditor::GetInstance()->Draw3D();

    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_)
            continue;
        if (obj->GetMaterialType() == 1) {
            obj->Draw(pointLightRes, spotLightRes);
        }
    }

    particleSystem_->Draw();

    DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);

    const bool grabUpdated =
        DrawSpecialMaterialObjects(objects, dxCommon_, BulletManager::GetInstance(), player_, isFirstPerson);
    DrawGPUParticles(dxCommon_, camera, gpuParticleTexHandle_, grabUpdated);
}

void GameOverScene::DrawUI() {
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());

    for (const FallingCrown& crown : fallingCrowns_) {
        if (crown.sprite && IsAlive(crown.sprite)) {
            crown.sprite->Draw();
        }
    }

    for (const DizzyStar& star : dizzyStars_) {
        if (star.sprite && IsAlive(star.sprite)) {
            star.sprite->Draw();
        }
    }

    for (auto& sprite : sprites_) {
        if (!sprite) {
            continue;
        }
        const std::string& spriteName = sprite->GetName();
        if (spriteName == "gameover_background") {
            continue;
        }
        if (spriteName.rfind(kFallingCrownSpritePrefix, 0) == 0) {
            continue;
        }
        if (spriteName.rfind(kDizzyStarSpritePrefix, 0) == 0) {
            continue;
        }
        sprite->Draw();
    }
}

void GameOverScene::DrawImGui() {
#ifdef USE_IMGUI
    if (ImGui::Begin("ゲームオーバー演出調整")) {
        ImGui::TextUnformatted("うつ伏せで潰れて待機 → 起き上がる → 右へ退場");
        ImGui::SliderFloat("タイトル表示開始", &presentationTuning_.titleRevealStartTime, 0.0f, 2.0f, "%.2f 秒");
        ImGui::DragFloat3("固定カメラ位置", &presentationTuning_.settleEyeOffset.x, 0.03f);
        ImGui::DragFloat3("固定注視位置", &presentationTuning_.settleTargetOffset.x, 0.03f);
        ImGui::SliderFloat("固定FOV", &presentationTuning_.settleFov, 0.30f, 1.00f, "%.3f");
        if (ImGui::Button("演出調整を既定値に戻す"))
            presentationTuning_ = PresentationTuning{};
        ImGui::TextUnformatted("UI配置はSprite Editorで調整できます。");
    }
    ImGui::End();
#endif
}

void GameOverScene::BindLayoutSprites() {
    backgroundSprite_ = FindSprite("gameover_background");

    constexpr std::array<const char*, 8> titleNames = {"gameover_letter_g",  "gameover_letter_a", "gameover_letter_m",
                                                       "gameover_letter_e1", "gameover_letter_o", "gameover_letter_v",
                                                       "gameover_letter_e2", "gameover_letter_r"};

    for (size_t i = 0; i < titleNames.size(); ++i) {
        titleLetters_[i] = FindSprite(titleNames[i]);
        if (titleLetters_[i]) {
            titleLetterBasePositions_[i] = titleLetters_[i]->GetPosition();
            titleLetterBaseSizes_[i] = titleLetters_[i]->GetSize();
        }
    }

    constexpr std::array<const char*, 2> rowPrefixes = {"gameover_retry", "gameover_title"};

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

void GameOverScene::RefreshLayoutSpritePointers() {
    backgroundSprite_ = IsAlive(backgroundSprite_) ? backgroundSprite_ : FindSprite("gameover_background");

    constexpr std::array<const char*, 8> titleNames = {"gameover_letter_g",  "gameover_letter_a", "gameover_letter_m",
                                                       "gameover_letter_e1", "gameover_letter_o", "gameover_letter_v",
                                                       "gameover_letter_e2", "gameover_letter_r"};

    for (size_t i = 0; i < titleNames.size(); ++i) {
        if (!IsAlive(titleLetters_[i])) {
            titleLetters_[i] = FindSprite(titleNames[i]);
            if (titleLetters_[i]) {
                titleLetterBasePositions_[i] = titleLetters_[i]->GetPosition();
                titleLetterBaseSizes_[i] = titleLetters_[i]->GetSize();
            } else {
                titleLetterBasePositions_[i] = {0.0f, 0.0f};
                titleLetterBaseSizes_[i] = {0.0f, 0.0f};
            }
        }
    }

    constexpr std::array<const char*, 2> rowPrefixes = {"gameover_retry", "gameover_title"};

    for (size_t i = 0; i < rowPrefixes.size(); ++i) {
        MenuRow& row = menuRows_[i];
        const std::string prefix = rowPrefixes[i];
        if (!IsAlive(row.backdrop)) {
            row.backdrop = FindSprite(prefix + "_row");
            row.backdropBaseSize = row.backdrop ? row.backdrop->GetSize() : Vector2{0.0f, 0.0f};
        }
        if (!IsAlive(row.label)) {
            row.label = FindSprite(prefix + "_label");
            row.labelBaseSize = row.label ? row.label->GetSize() : Vector2{0.0f, 0.0f};
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

void GameOverScene::InitializeFallingCrowns() {
    fallingCrowns_.clear();
    if (!spriteCommon_) {
        return;
    }

    // 背景を落下する王冠の初期レイアウトと周期差。個々の開始時刻をずらして同時に落ちるのを避ける。
    struct CrownSetup {
        float x;
        float y;
        float width;
        float timeOffset;
        float cycleDuration;
        float driftAmplitude;
        float driftSpeed;
        float rotationSpeed;
        float phase;
        float alpha;
    };

    constexpr std::array<CrownSetup, 10> setups = {
        CrownSetup{170.0f, -170.0f, 122.0f, 1.5f, 24.0f, 26.0f, 0.55f, 0.10f, 0.1f, 0.68f},
        CrownSetup{380.0f, -220.0f, 86.0f, 9.2f, 27.0f, 18.0f, 0.62f, -0.08f, 1.4f, 0.48f},
        CrownSetup{650.0f, -185.0f, 104.0f, 14.0f, 29.0f, 22.0f, 0.48f, 0.07f, 2.1f, 0.52f},
        CrownSetup{950.0f, -260.0f, 76.0f, 4.4f, 25.0f, 16.0f, 0.68f, -0.06f, 2.9f, 0.40f},
        CrownSetup{1250.0f, -190.0f, 112.0f, 12.5f, 28.0f, 24.0f, 0.52f, 0.08f, 3.8f, 0.58f},
        CrownSetup{1535.0f, -235.0f, 92.0f, 6.8f, 26.0f, 20.0f, 0.58f, -0.09f, 4.4f, 0.46f},
        CrownSetup{1745.0f, -155.0f, 132.0f, 18.3f, 31.0f, 28.0f, 0.44f, 0.06f, 5.2f, 0.60f},
        CrownSetup{290.0f, -360.0f, 72.0f, 20.5f, 30.0f, 16.0f, 0.70f, 0.11f, 5.9f, 0.30f},
        CrownSetup{1380.0f, -325.0f, 70.0f, 22.0f, 32.0f, 14.0f, 0.66f, -0.10f, 6.6f, 0.34f},
        CrownSetup{1110.0f, -300.0f, 82.0f, 24.0f, 33.0f, 18.0f, 0.50f, 0.07f, 7.4f, 0.36f},
    };

    const uint32_t textureHandle = Sprite::LoadTexture("ui/gameover/gameover_crown_falling.png");
    for (size_t i = 0; i < setups.size(); ++i) {
        const CrownSetup& setup = setups[i];
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_.get(), textureHandle);
        sprite->SetName(std::string(kFallingCrownSpritePrefix) + std::to_string(i));
        sprite->SetAnchorPoint({0.5f, 0.5f});
        sprite->SetSize({setup.width, setup.width * 0.75f});
        sprite->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
        sprite->SetVisible(false);

        Sprite* spritePtr = sprite.get();
        sprites_.push_back(std::move(sprite));
        fallingCrowns_.push_back(FallingCrown{spritePtr, Vector2{setup.x, setup.y},
                                              Vector2{setup.width, setup.width * 0.75f}, setup.timeOffset,
                                              setup.cycleDuration, 1420.0f, setup.driftAmplitude, setup.driftSpeed,
                                              setup.rotationSpeed, setup.phase, setup.alpha});
    }

    UpdateFallingCrowns(0.0f);
}

void GameOverScene::UpdateFallingCrowns(float deltaTime) {
    (void)deltaTime;
    const float presentationAlpha = EaseOutCubic((sceneTime_ - 0.48f) / 0.82f);

    for (FallingCrown& crown : fallingCrowns_) {
        if (!crown.sprite || !IsAlive(crown.sprite)) {
            crown.sprite = nullptr;
            continue;
        }

        float localTime = std::fmod(sceneTime_ + crown.timeOffset, crown.cycleDuration);
        if (localTime < 0.0f) {
            localTime += crown.cycleDuration;
        }

        const float fallRate = localTime / crown.cycleDuration;
        const float y = crown.basePosition.y + crown.fallDistance * fallRate;
        const float x =
            crown.basePosition.x + std::sin(localTime * crown.driftSpeed + crown.phase) * crown.driftAmplitude;
        const float fadeIn = Clamp01((y + 120.0f) / 220.0f);
        const float fadeOut = Clamp01((1180.0f - y) / 260.0f);
        const float alpha = std::min(fadeIn, fadeOut) * crown.alpha * presentationAlpha;
        const float slowTilt = std::sin(localTime * 0.42f + crown.phase) * 0.16f;

        crown.sprite->SetVisible(alpha > 0.02f);
        crown.sprite->SetPosition({x, y});
        crown.sprite->SetSize(crown.size);
        crown.sprite->SetRotation(slowTilt + localTime * crown.rotationSpeed);
        crown.sprite->SetColor({1.0f, 1.0f, 1.0f, alpha});
    }
}

void GameOverScene::InitializeDizzyStars() {
    dizzyStars_.clear();
    if (!spriteCommon_) {
        return;
    }

    const uint32_t textureHandle = Sprite::LoadTexture("ui/gameover/gameover_dizzy_star.png");
    constexpr std::array<float, 3> baseAngles = {0.0f, 2.0943951f, 4.1887902f};
    constexpr std::array<float, 3> sizes = {42.0f, 34.0f, 38.0f};

    for (size_t i = 0; i < baseAngles.size(); ++i) {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_.get(), textureHandle);
        sprite->SetName(std::string(kDizzyStarSpritePrefix) + std::to_string(i));
        sprite->SetAnchorPoint({0.5f, 0.5f});
        sprite->SetSize({sizes[i], sizes[i]});
        sprite->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
        sprite->SetVisible(false);

        Sprite* spritePtr = sprite.get();
        sprites_.push_back(std::move(sprite));
        dizzyStars_.push_back(DizzyStar{spritePtr, baseAngles[i], sizes[i], static_cast<float>(i) * 0.18f});
    }

    UpdateDizzyStars(0.0f);
}

void GameOverScene::UpdateDizzyStars(float deltaTime) {
    (void)deltaTime;

    Vector2 center = {static_cast<float>(WinApp::kClientWidth) * 0.5f,
                      static_cast<float>(WinApp::kClientHeight) * 0.57f};

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera && IsAlive(gameOverSlimeObject_)) {
        const Vector3 headWorld = gameOverSlimeAnimator_.GetDizzyAnchorWorld();

        const Matrix4x4 viewProjection = camera->GetViewProjectionMatrix();
        const Matrix4x4 viewport = Math::MakeViewportMatrix(0.0f, 0.0f, static_cast<float>(WinApp::kClientWidth),
                                                            static_cast<float>(WinApp::kClientHeight), 0.0f, 1.0f);
        const Matrix4x4 screenMatrix = Math::Multiply(viewProjection, viewport);
        const Vector3 screen = Math::Transform(headWorld, screenMatrix);

        if (screen.z >= 0.0f && screen.z <= 1.0f) {
            center = {screen.x, screen.y + 10.0f};
        }
    }

    const float animationTime = gameOverSlimeAnimator_.GetTimer();
    const float introAlpha = EaseOutCubic((animationTime - 0.16f) / 0.42f) * gameOverSlimeAnimator_.GetDizzyAlpha();
    const float orbitTime = animationTime * 2.45f;
    const float uiScale = static_cast<float>(WinApp::kClientHeight) / 1080.0f;
    const float radiusX = 46.0f * uiScale;
    const float radiusY = 15.0f * uiScale;

    for (DizzyStar& star : dizzyStars_) {
        if (!star.sprite || !IsAlive(star.sprite)) {
            star.sprite = nullptr;
            continue;
        }

        const float angle = orbitTime + star.baseAngle;
        const float depth = 0.5f + 0.5f * std::sin(angle);
        const float size = star.size * (1.0f + depth * 0.15f) * uiScale;
        const float wobble = std::sin(animationTime * 3.0f + star.baseAngle) * 1.5f * uiScale;
        const Vector2 position = {center.x + std::cos(angle) * radiusX, center.y + std::sin(angle) * radiusY + wobble};

        star.sprite->SetVisible(introAlpha > 0.02f);
        star.sprite->SetPosition(position);
        star.sprite->SetSize({size, size});
        star.sprite->SetRotation(-angle * 0.45f);
        star.sprite->SetColor({1.0f, 1.0f, 1.0f, 1.0f * introAlpha});
    }
}

void GameOverScene::UpdateMenuInput() {
    if (!inputManager_) {
        return;
    }

    const bool up = inputManager_->IsKeyTriggered(DIK_UP) || inputManager_->IsKeyTriggered(DIK_W) ||
                    inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP);
    const bool down = inputManager_->IsKeyTriggered(DIK_DOWN) || inputManager_->IsKeyTriggered(DIK_S) ||
                      inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN);

    if (up) {
        ChangeSelection(-1);
    }
    if (down) {
        ChangeSelection(1);
    }

    if (inputManager_->IsKeyTriggered(DIK_SPACE) || inputManager_->IsKeyTriggered(DIK_RETURN) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
        ConfirmSelection();
    }
}

// リトライ時は残機を戻し、スライムの退場動作を待ってからゲームへ遷移します。
void GameOverScene::StartRetryExit() {
    if (retryExitActive_ || retrySceneChangeRequested_) {
        return;
    }

    GameDataManager::GetInstance()->ResetLives();
    retryExitActive_ = true;
    retrySceneChangeRequested_ = false;
    retryExitTimer_ = 0.0f;

    if (!gameOverSlimeObject_) {
        FindGameOverSlimeObject();
    }

    Vector3 exitDirection = {1.0f, 0.0f, 0.0f};
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        const Vector3 rotation = camera->GetRotation();
        exitDirection = {std::cos(rotation.y), 0.0f, -std::sin(rotation.y)};
    }

    if (gameOverSlimeObject_) {
        gameOverSlimeAnimator_.StartExitRight(gameOverSlimeObject_, exitDirection, 11.5f);
    } else {
        retrySceneChangeRequested_ = true;
        SceneManager::GetInstance()->ChangeSceneWithoutLoading("GAMEPLAY");
    }

    DebugConsole::GetInstance()->AddLog("[GameOver] Retry selected. Slime exit animation started.");
}

void GameOverScene::UpdateRetryExit(float deltaTime) {
    if (!retryExitActive_) {
        return;
    }

    retryExitTimer_ += deltaTime;

    if (!retrySceneChangeRequested_ && (gameOverSlimeAnimator_.IsExitAnimationFinished() || retryExitTimer_ >= 4.0f)) {
        retrySceneChangeRequested_ = true;
        SceneManager::GetInstance()->ChangeSceneWithoutLoading("GAMEPLAY");
    }
}

void GameOverScene::UpdateMenuSprites(float deltaTime) {
    (void)deltaTime;
    RefreshLayoutSpritePointers();
    // 編集停止中は登場タイマーが進まないので、配置確認用に完成状態を表示する。
    const bool layoutPreview = !SceneManager::GetInstance()->IsPlaying();
    const float visibleTitleTime = layoutPreview ? 100.0f : titleRevealTimer_;

    backgroundSprite_ = FindSprite("gameover_background");
    if (backgroundSprite_) {
        backgroundSprite_->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    }

    const Vector4 hiddenTitleColor = {0.72f, 0.94f, 1.0f, 0.0f};

    for (size_t i = 0; i < titleLetters_.size(); ++i) {
        Sprite* letter = titleLetters_[i];
        if (!letter) {
            continue;
        }

        const float appearTime = static_cast<float>(i) * kTitleLetterInterval;
        const float revealElapsed = visibleTitleTime - appearTime;
        const float wave = std::sin(sceneTime_ * 2.2f + static_cast<float>(i) * 0.42f);
        const float pulse = 0.5f + 0.5f * std::sin(sceneTime_ * 3.0f + static_cast<float>(i) * 0.28f);
        const Vector2 basePos = titleLetterBasePositions_[i];
        const Vector2 baseSize = titleLetterBaseSizes_[i];

        if (revealElapsed <= 0.0f) {
            letter->SetVisible(false);
            letter->SetPosition(basePos);
            letter->SetSize({baseSize.x * 0.65f, baseSize.y * 0.65f});
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

        letter->SetPosition({basePos.x, basePos.y + dropOffset + wave * 4.0f * revealEase});
        letter->SetSize({baseSize.x * scale, baseSize.y * scale});
        letter->SetColor({0.84f + pulse * 0.16f, 0.95f + pulse * 0.05f, 1.0f, revealEase});
    }

    const float pulse = 0.5f + 0.5f * std::sin(sceneTime_ * 5.4f);
    const Vector4 normalText = {0.18f, 0.20f, 0.34f, 1.0f};
    const Vector4 selectedText = {0.10f, 0.12f, 0.23f, 1.0f};
    const float menuStartTime = static_cast<float>(titleLetters_.size()) * kTitleLetterInterval + kMenuRevealDelay;
    const float retryMenuFade =
        retryExitActive_ && !layoutPreview ? (1.0f - EaseOutCubic(retryExitTimer_ / 0.28f)) : 1.0f;
    const float menuAlpha = EaseOutCubic((visibleTitleTime - menuStartTime) / 0.28f) * retryMenuFade;
    const bool menuVisible = menuAlpha > 0.0f;

    for (int i = 0; i < static_cast<int>(MenuItem::Count); ++i) {
        MenuRow& row = menuRows_[static_cast<size_t>(i)];
        const bool selected = selectedIndex_ == i;
        const float rowScale = selected ? 1.025f + pulse * 0.006f : 1.0f;
        const float labelScale = selected ? 1.02f : 1.0f;

        if (row.backdrop) {
            row.backdrop->SetVisible(menuVisible);
            const uint32_t handle = Sprite::LoadTexture(selected ? "ui/gameover/gameover_button_selected.png"
                                                                 : "ui/gameover/gameover_button_normal.png");
            row.backdrop->SetTextureHandle(handle);
            const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
            row.backdrop->SetTextureRect({0.0f, 0.0f},
                                         {static_cast<float>(metadata.width), static_cast<float>(metadata.height)});
            row.backdrop->SetSize({row.backdropBaseSize.x * rowScale, row.backdropBaseSize.y * rowScale});
            Vector4 backdropColor = selected ? Vector4{1.0f, 1.0f, 1.0f, 0.98f} : Vector4{1.0f, 1.0f, 1.0f, 0.90f};
            backdropColor.w *= menuAlpha;
            row.backdrop->SetColor(backdropColor);
        }

        if (row.label) {
            row.label->SetVisible(menuVisible);
            row.label->SetSize({row.labelBaseSize.x * labelScale, row.labelBaseSize.y * labelScale});
            Vector4 labelColor = selected ? selectedText : normalText;
            labelColor.w *= menuAlpha;
            row.label->SetColor(labelColor);
        }
    }
}

bool GameOverScene::IsTitleRevealComplete() const {
    const float menuStartTime = static_cast<float>(titleLetters_.size()) * kTitleLetterInterval + kMenuRevealDelay;
    return titleRevealTimer_ >= menuStartTime + 0.28f;
}

void GameOverScene::InitializeGameOverPresentation() {
    FindGameOverSlimeObject();
    // 作者が配置した基準位置を、倒れ姿勢を適用する前に保存する。
    gameOverPresentationTimer_ = 0.0f;
    gameOverImpactCuePlayed_ = false;

    if (gameOverSlimeObject_) {
        gameOverSlimeBasePosition_ = gameOverSlimeObject_->GetWorldPosition();
        gameOverSlimeBaseScale_ = gameOverSlimeObject_->GetScale();
    }

    gameOverSlimeAnimator_.Reset(gameOverSlimeObject_);
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->SetFollowTarget(nullptr);
        camera->SetInputEnabled(false);
        camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    }

    UpdateGameOverPresentation(0.0f);
}

void GameOverScene::FindGameOverSlimeObject() {
    gameOverSlimeObject_ = nullptr;
    if (!objectManager_) {
        return;
    }

    for (const auto& object : objectManager_->GetObjects()) {
        if (!object) {
            continue;
        }

        const std::string& name = object->GetName();
        const std::string modelName = object->GetModelName();
        if (name == "Characters/slime_1" || modelName == "Characters/slime" ||
            modelName.find("Characters/slime") != std::string::npos) {
            gameOverSlimeObject_ = object.get();
            return;
        }
    }
}

void GameOverScene::UpdateGameOverPresentation(float deltaTime) {
    if (!IsAlive(gameOverSlimeObject_)) {
        gameOverSlimeObject_ = nullptr;
    }
    if (!gameOverSlimeObject_) {
        FindGameOverSlimeObject();
        if (gameOverSlimeObject_) {
            gameOverSlimeAnimator_.Reset(gameOverSlimeObject_);
        }
    }

    if (!gameOverSlimeObject_) {
        return;
    }

    gameOverPresentationTimer_ += std::max(0.0f, deltaTime);
    gameOverSlimeAnimator_.Update(gameOverSlimeObject_, deltaTime);

    if (!gameOverImpactCuePlayed_ && gameOverPresentationTimer_ >= GameOverSlimeAnimator::GetImpactTime()) {
        // 倒れた状態から始めるため、着地VFXは発生させない。
        gameOverImpactCuePlayed_ = true;
    }

    UpdateGameOverPostEffects();
}

void GameOverScene::UpdateGameOverCamera() {
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera || !gameOverSlimeObject_)
        return;
    // 2D背景の床と3Dキャラクターがずれないよう、退場まで同じ構図を維持する。
    const Vector3 eye = gameOverSlimeBasePosition_ + presentationTuning_.settleEyeOffset;
    const Vector3 target = gameOverSlimeBasePosition_ + presentationTuning_.settleTargetOffset;
    const Vector3 rotation = MakeLookAtEuler(eye, target);
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->ConfigFixedPoint(eye, rotation);
    camera->SetEye(eye);
    camera->SetTarget(target);
    camera->SetRotation(rotation);
    camera->SetFovY(presentationTuning_.settleFov);
}

void GameOverScene::UpdateGameOverPostEffects() {
    // 赤フラッシュ、色収差、ブラーで倒れ姿勢やメニューを覆わない。
    // シーン遷移用のフェード値はFade側の所有なので変更しない。
    ResetGameOverPostEffects();
}

void GameOverScene::EmitGameOverImpactCue() {
    if (!gameOverSlimeObject_) {
        return;
    }

    VFXSequencer::PlayOneShot(kGameOverImpactCue, gameOverSlimeObject_->GetWorldPosition());
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        camera->StartShake(0.24f, presentationTuning_.impactShake, 28.0f, {0.85f, 0.55f, 0.35f});
    }
}

void GameOverScene::ResetGameOverPostEffects() {
    PostEffect::Params* params = PostEffect::GetInstance()->GetParams();
    if (!params) {
        return;
    }

    params->vignetteIntensity = 0.0f;
    params->vignettePower = 1.0f;
    params->chromaticAberration = 0.0f;
    params->filmGrainIntensity = 0.0f;
    params->radialIntensity = 0.0f;
    params->colorExposure = 0.0f;
    params->colorContrast = 1.0f;
    params->colorSaturation = 1.0f;
    params->colorTemperature = 0.0f;
    params->damageFlash = 0.0f;
    params->cinemaBarHeight = 0.0f;
    params->bloomIntensity = 0.0f;
    params->dangerVignette = 0.0f;
    params->wobbleIntensity = 0.0f;
    params->colorTint = 0.0f;
    params->scanlineIntensity = 0.0f;
    params->mosaicSize = 0.0f;
    params->grayscaleIntensity = 0.0f;
    params->sepiaIntensity = 0.0f;
    params->boxFilterSize = 0;
    params->gaussianFilterSize = 0;
    params->luminanceOutlineIntensity = 0.0f;
    params->depthOutlineIntensity = 0.0f;
    params->randomIntensity = 0.0f;
    params->dissolveThreshold = 0.0f;
}

void GameOverScene::DrawBackgroundSprite() {
    backgroundSprite_ = IsAlive(backgroundSprite_) ? backgroundSprite_ : FindSprite("gameover_background");
    if (!backgroundSprite_) {
        return;
    }

    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    backgroundSprite_->Draw();
}

void GameOverScene::ChangeSelection(int direction) {
    const int count = static_cast<int>(MenuItem::Count);
    selectedIndex_ = (selectedIndex_ + direction + count) % count;
}

void GameOverScene::ConfirmSelection() {
    if (retryExitActive_ || retrySceneChangeRequested_ || !IsTitleRevealComplete()) {
        return;
    }

    switch (static_cast<MenuItem>(selectedIndex_)) {
    case MenuItem::Retry:
        StartRetryExit();
        break;
    case MenuItem::Title:
        retrySceneChangeRequested_ = true;
        DebugConsole::GetInstance()->AddLog("[GameOver] Return title selected.");
        SceneManager::GetInstance()->ChangeSceneWithoutLoading("TITLE");
        break;
    default:
        break;
    }
}

void GameOverScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

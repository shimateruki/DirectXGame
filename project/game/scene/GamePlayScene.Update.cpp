#define NOMINMAX
#include "GamePlayScene.h"

#include "BulletManager.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include "Fade.h"
#include "GPUParticleManager.h"
#include "GameDataManager.h"
#include "GameSettingsManager.h"
#include "InputManager.h"
#include "LightEditor.h"
#include "LockOnSystem.h"
#include "MeshEffectManager.h"
#include "ObjectManager.h"
#include "ParticleSystem.h"
#include "PostEffect.h"
#include "ProfilerManager.h"
#include "SceneManager.h"
#include "StageManager.h"
#include "WinApp.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGoalReadyTime = 2.35f;
constexpr float kGoalAutoReturnTime = 4.15f;
constexpr float kGoalStarEmitInterval = 0.13f;
constexpr float kPi = 3.1415926535f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr const char* kGoalPlayerStarPreset = "crown_get_twinkle_fountain";
constexpr const char* kGoalPlayerAfterglowPreset = "crown_get_afterglow";

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseOut(float value) {
    value = Clamp01(value);
    const float inv = 1.0f - value;
    return 1.0f - inv * inv * inv;
}

float EaseInOut(float value) {
    value = Clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

float NormalizeYaw(float yaw) {
    while (yaw > kPi) yaw -= kTwoPi;
    while (yaw < -kPi) yaw += kTwoPi;
    return yaw;
}

float SafeAtan2Yaw(const Vector3& from, const Vector3& to, float fallbackYaw) {
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    if (std::abs(dx) < 0.001f && std::abs(dz) < 0.001f) {
        return fallbackYaw;
    }
    return std::atan2(dx, dz);
}
}

void GamePlayScene::SetIsGoal(bool isGoal) {
    if (isGoal_ == isGoal) {
        return;
    }

    isGoal_ = isGoal;
    if (!isGoal_) {
        goalSavePerformed_ = false;
        goalPresentationState_ = GoalPresentationState::Inactive;
        goalPresentationTimer_ = 0.0f;
        goalStarEmitTimer_ = 0.0f;
        goalPlayerSnapshotValid_ = false;
    }
}

void GamePlayScene::StartGoalPresentation(Object3d* crownObject) {
    if (isGoal_ && goalPresentationState_ != GoalPresentationState::Inactive) {
        return;
    }

    SetIsGoal(true);
    goalPresentationState_ = GoalPresentationState::Celebrating;
    goalPresentationTimer_ = 0.0f;
    goalStarEmitTimer_ = 0.0f;
    goalCrownPosition_ = crownObject ? crownObject->GetWorldPosition() : (player_ ? player_->GetWorldPosition() : Vector3{ 0.0f, 0.0f, 0.0f });

    if (player_) {
        goalSavedPlayerControlActive_ = player_->IsControlActive();
        goalPlayerBasePosition_ = player_->GetTransform()->translate;
        goalPlayerBaseScale_ = player_->GetScale();
        goalPlayerBaseRotation_ = player_->GetRotation();
        goalPlayerSnapshotValid_ = true;

        player_->SetIsControlActive(false);
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });

        const float faceYaw = SafeAtan2Yaw(goalPlayerBasePosition_, goalCrownPosition_, goalPlayerBaseRotation_.y - player_->GetVisualYawOffset());
        Vector3 rotation = goalPlayerBaseRotation_;
        rotation.y = NormalizeYaw(faceYaw + player_->GetVisualYawOffset());
        player_->SetRotation(rotation);
        goalPlayerBaseRotation_ = rotation;
    }

    UpdateGoalPresentationOverlay();
}

void GamePlayScene::Update(float deltaTime) {
    if (HandleGoalClear(deltaTime)) {
        return;
    }

    if (HandlePauseOverlay(deltaTime)) {
        return;
    }

    UpdatePostEffectState(deltaTime);
    UpdateLockOnAndCamera(deltaTime);
    UpdateSceneSystems(deltaTime);
    UpdateUI(deltaTime);
    UpdateEffectDebugShortcuts();

    if (animatedCube_) {
        animatedCube_->Update(deltaTime);
    }
}

bool GamePlayScene::HandlePauseOverlay(float deltaTime) {
    if (settingsOverlay_ && settingsOverlay_->IsActive()) {
        settingsOverlay_->Update(deltaTime);
        return true;
    }

    if (pauseMenuOverlay_ && pauseMenuOverlay_->IsActive()) {
        PauseMenuOverlay::Action action = pauseMenuOverlay_->Update(deltaTime);
        switch (action) {
        case PauseMenuOverlay::Action::Resume:
            pauseMenuOverlay_->SetActive(false);
            break;
        case PauseMenuOverlay::Action::Retry:
            pauseMenuOverlay_->SetActive(false);
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            break;
        case PauseMenuOverlay::Action::StageSelect:
            pauseMenuOverlay_->SetActive(false);
            SceneManager::GetInstance()->ChangeScene("SELECT");
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
        case PauseMenuOverlay::Action::None:
        default:
            break;
        }
        return true;
    }

    if (IsPauseOpenTriggered()) {
        if (pauseMenuOverlay_) {
            pauseMenuOverlay_->SetActive(true);
        }
        return true;
    }

    return false;
}

bool GamePlayScene::IsPauseOpenTriggered() const {
    if (!inputManager_) {
        return false;
    }

#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard || io.WantTextInput) {
        return false;
    }
#endif

    if (lifeLostPresentationActive_ || lifeLostBlackHold_ || isGoal_) {
        return false;
    }

    return inputManager_->IsKeyTriggered(DIK_P) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_START);
}

bool GamePlayScene::HandleGoalClear(float& deltaTime) {
    if (!isGoal_) {
        return false;
    }

    if (!goalSavePerformed_) {
        int currentStage = StageManager::GetInstance()->GetCurrentStageIndex();
        auto* gameData = GameDataManager::GetInstance();
        const bool wasStageCleared = gameData->IsStageCleared(currentStage);
        const int previousCrownCount = gameData->GetClearedStageCount();
        gameData->MarkStageCleared(currentStage);
        const int newCrownCount = gameData->GetClearedStageCount();
        if (!wasStageCleared && newCrownCount > previousCrownCount) {
            gameData->RequestStageClearRewardPresentation(currentStage, previousCrownCount, newCrownCount);
        }

        for (int i = 0; i < 3; i++) {
            if (sessionStarCoins_[i]) {
                gameData->MarkStarCoinCollected(currentStage, i);
            }
        }

        if (saveIndicatorOverlay_) {
            saveIndicatorOverlay_->Play(1.35f);
        }
        DebugConsole::GetInstance()->AddLog("Saving stage clear data...");
        goalSavePerformed_ = true;
    }

    if (saveIndicatorOverlay_) {
        saveIndicatorOverlay_->Update(deltaTime);
    }
    UpdateGoalPresentation(deltaTime);
    deltaTime = 0.0f;

    if (goalPresentationState_ == GoalPresentationState::ReadyToReturn &&
        inputManager_ &&
        inputManager_->IsKeyTriggered(DIK_SPACE)) {
        RequestGoalReturnToSelect();
    }

    if (goalPresentationState_ == GoalPresentationState::Returning) {
        return true;
    }

    return false;
}

void GamePlayScene::UpdateGoalPresentation(float deltaTime) {
    if (goalPresentationState_ == GoalPresentationState::Inactive ||
        goalPresentationState_ == GoalPresentationState::Returning) {
        return;
    }

    goalPresentationTimer_ += deltaTime;
    UpdateGoalPlayerCelebration(deltaTime);
    UpdateGoalPresentationOverlay();

    if (goalPresentationState_ == GoalPresentationState::Celebrating &&
        goalPresentationTimer_ >= kGoalReadyTime) {
        goalPresentationState_ = GoalPresentationState::ReadyToReturn;
    }

    if (goalPresentationTimer_ >= kGoalAutoReturnTime) {
        RequestGoalReturnToSelect();
    }
}

void GamePlayScene::UpdateGoalPlayerCelebration(float deltaTime) {
    if (!player_ || !goalPlayerSnapshotValid_) {
        return;
    }

    Transform* transform = player_->GetTransform();
    const float t = goalPresentationTimer_;

    Vector3 position = goalPlayerBasePosition_;
    Vector3 scale = goalPlayerBaseScale_;
    Vector3 rotation = goalPlayerBaseRotation_;

    if (t < 0.22f) {
        const float e = EaseInOut(t / 0.22f);
        scale.x = goalPlayerBaseScale_.x * (1.0f + 0.22f * e);
        scale.y = goalPlayerBaseScale_.y * (1.0f - 0.26f * e);
        scale.z = goalPlayerBaseScale_.z * (1.0f + 0.22f * e);
    } else if (t < 0.95f) {
        const float p = Clamp01((t - 0.22f) / 0.73f);
        const float jump = std::sin(p * kPi);
        const float takeoffStretch = std::sin(Clamp01(p * 1.8f) * kPi);
        position.y += jump * 1.15f;
        scale.x = goalPlayerBaseScale_.x * (1.0f - 0.12f * takeoffStretch);
        scale.y = goalPlayerBaseScale_.y * (1.0f + 0.18f * takeoffStretch);
        scale.z = goalPlayerBaseScale_.z * (1.0f - 0.12f * takeoffStretch);
        rotation.y = NormalizeYaw(goalPlayerBaseRotation_.y + kTwoPi * EaseInOut(p));
    } else if (t < 1.35f) {
        const float p = Clamp01((t - 0.95f) / 0.40f);
        const float squash = std::sin(p * kPi);
        scale.x = goalPlayerBaseScale_.x * (1.0f + 0.24f * squash);
        scale.y = goalPlayerBaseScale_.y * (1.0f - 0.22f * squash);
        scale.z = goalPlayerBaseScale_.z * (1.0f + 0.24f * squash);
        rotation.y = NormalizeYaw(goalPlayerBaseRotation_.y + kTwoPi);
    } else {
        const float p = t - 1.35f;
        const float bob = std::sin(p * 8.0f) * 0.065f;
        const float wobble = std::sin(p * 11.0f) * std::max(0.0f, 1.0f - p * 0.38f);
        position.y += std::max(0.0f, bob);
        scale.x = goalPlayerBaseScale_.x * (1.0f + 0.035f * wobble);
        scale.y = goalPlayerBaseScale_.y * (1.0f - 0.028f * wobble);
        scale.z = goalPlayerBaseScale_.z * (1.0f - 0.020f * wobble);
        rotation.z = goalPlayerBaseRotation_.z + 0.12f * wobble;
        rotation.y = NormalizeYaw(goalPlayerBaseRotation_.y + kTwoPi);
    }

    transform->translate = position;
    player_->SetRotation(rotation);
    player_->SetScale(scale);
    player_->UpdateLocalMatrix();
    player_->UpdateWorldMatrix();

    if (t <= 2.45f) {
        goalStarEmitTimer_ -= deltaTime;
        if (goalStarEmitTimer_ <= 0.0f) {
            Vector3 emitPos = position;
            emitPos.y += std::max(1.4f, scale.y * 0.82f);
            GPUParticleManager::GetInstance()->Emit(kGoalPlayerStarPreset, emitPos);
            if (t > 1.05f) {
                GPUParticleManager::GetInstance()->Emit(kGoalPlayerAfterglowPreset, emitPos);
            }
            goalStarEmitTimer_ = kGoalStarEmitInterval;
        }
    }
}

void GamePlayScene::RequestGoalReturnToSelect() {
    if (goalPresentationState_ == GoalPresentationState::Returning) {
        return;
    }

    goalPresentationState_ = GoalPresentationState::Returning;
    if (player_ && goalPlayerSnapshotValid_) {
        player_->SetIsControlActive(goalSavedPlayerControlActive_);
    }
    SceneManager::GetInstance()->ChangeScene("SELECT");
}

void GamePlayScene::UpdatePostEffectState(float deltaTime) {
    PostEffect::GetInstance()->Update(deltaTime);

    Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
    if (activeCamera) {
        PostEffect::GetInstance()->GetParams()->projectionInverse = Math::Inverse(activeCamera->GetProjectionMatrix());
    }
}

void GamePlayScene::UpdateLockOnAndCamera(float deltaTime) {
    LightEditor::GetInstance()->Update();

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        isDrawLockOn_ = false;
        return;
    }

    lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_);
    CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());

    Object3d* target = lockOnSystem_->GetTarget();
    if (target && lockOnSystem_->IsLockingOn()) {
        UpdateLockOnSprite(camera, target, deltaTime);
    } else {
        isDrawLockOn_ = false;
    }

    if (!CameraEditor::GetInstance()->IsEditorMode()) {
        Camera::FollowMode currentMode = camera->GetFollowMode();
        if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
            camera->SetRotationSensitivity(GameSettingsManager::GetInstance()->GetCameraSensitivity());
            Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
            if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
                camera->AddRotation(mouseDelta);
            }
        }
    }
}

void GamePlayScene::UpdateLockOnSprite(Camera* camera, Object3d* target, float deltaTime) {
    if (!camera || !target || !lockOnSprite_) {
        isDrawLockOn_ = false;
        return;
    }

    isDrawLockOn_ = true;

    const AABB aabb = target->GetAABB();
    Vector3 targetCenter;
    targetCenter.x = (aabb.min.x + aabb.max.x) * 0.5f;
    targetCenter.y = (aabb.min.y + aabb.max.y) * 0.5f;
    targetCenter.z = (aabb.min.z + aabb.max.z) * 0.5f;

    Math math;
    const Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    const float w =
        targetCenter.x * viewProj.m[0][3] +
        targetCenter.y * viewProj.m[1][3] +
        targetCenter.z * viewProj.m[2][3] +
        viewProj.m[3][3];

    if (w <= 0.001f) {
        isDrawLockOn_ = false;
        return;
    }

    Vector3 ndc;
    ndc.x = (
        targetCenter.x * viewProj.m[0][0] +
        targetCenter.y * viewProj.m[1][0] +
        targetCenter.z * viewProj.m[2][0] +
        viewProj.m[3][0]
        ) / w;
    ndc.y = (
        targetCenter.x * viewProj.m[0][1] +
        targetCenter.y * viewProj.m[1][1] +
        targetCenter.z * viewProj.m[2][1] +
        viewProj.m[3][1]
        ) / w;

    const float screenWidth = static_cast<float>(::WinApp::kClientWidth);
    const float screenHeight = static_cast<float>(::WinApp::kClientHeight);
    const float screenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
    const float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;
    lockOnSprite_->SetPosition({ screenX, screenY });

    const float objSizeX = aabb.max.x - aabb.min.x;
    const float objSizeY = aabb.max.y - aabb.min.y;
    const float objSizeZ = aabb.max.z - aabb.min.z;
    const float maxObjSize = std::max({ objSizeX, objSizeY, objSizeZ });
    const float baseSize = maxObjSize * 25.0f;
    const float distanceScale = 20.0f / w;
    const float finalSize = std::clamp(baseSize * distanceScale, 32.0f, 256.0f);

    lockOnSprite_->SetSize({ finalSize, finalSize });
    lockOnSprite_->SetRotation(lockOnSprite_->GetRotation() + 2.0f * deltaTime);
    lockOnSprite_->Update();
}

void GamePlayScene::UpdateSceneSystems(float deltaTime) {
    ProfilerManager::GetInstance()->SetObjectList(&objectManager_->GetObjects());
    CameraManager::GetInstance()->Update();
    particleSystem_->Update(deltaTime);
    objectManager_->Update(deltaTime);
    GPUParticleManager::GetInstance()->Update(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }

    BulletManager::GetInstance()->Update(deltaTime);
    {
        PROFILE_SCOPE("衝突判定");
        CollisionManager::GetInstance()->Update();
    }
}

void GamePlayScene::UpdateEffectDebugShortcuts() {
#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard || io.WantTextInput) {
        return;
    }
    if (!inputManager_->IsKeyPressed(DIK_LCONTROL) && !inputManager_->IsKeyPressed(DIK_RCONTROL)) {
        return;
    }
#else
    return;
#endif

    if (inputManager_->IsKeyTriggered(DIK_SPACE)) {
        Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
        particleSystem_->SpawnStarHitEffect(effectPos);
        particleSystem_->SpawnSlashEffect(effectPos);
    }

    if (inputManager_->IsKeyTriggered(DIK_RETURN)) {
        Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
        particleSystem_->SpawnStarHitEffect(effectPos);
    }

    if (inputManager_->IsKeyTriggered(DIK_E)) {
        Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
        particleSystem_->SpawnSlashEffect(effectPos);
    }

    if (inputManager_->IsKeyTriggered(DIK_R)) {
        Vector3 effectPos = { 0.0f, 2.0f, 0.0f };
        MeshEffectManager::GetInstance()->SpawnRingWaveEffect(effectPos);
    }

    if (inputManager_->IsKeyTriggered(DIK_T)) {
        Vector3 effectPos = { 0.0f, 1.5f, 0.0f };
        MeshEffectManager::GetInstance()->SpawnPortalEffect(effectPos, 5.0f);
    }

    if (inputManager_->IsKeyTriggered(DIK_Y)) {
        Vector3 spawnPos = { 0.0f, 0.01f, 10.0f };
        if (player_) {
            Vector3 playerPos = player_->GetWorldPosition();
            Vector3 forward = player_->GetForwardDirection();
            spawnPos = {
                playerPos.x + forward.x * 3.0f,
                playerPos.y,
                playerPos.z + forward.z * 3.0f
            };
        }

        MeshEffectManager::GetInstance()->SpawnEffectAt("Resources/json/effect/effect_warp_gate_floor.json", spawnPos, { 0.0f, 0.0f, 0.0f });
        MeshEffectManager::GetInstance()->SpawnEffectAt("Resources/json/effect/effect_warp_gate_pillar.json", spawnPos, { 0.0f, 0.0f, 0.0f });
    }
}

#define NOMINMAX
#include "GamePlayScene.h"
#include "SceneController.h"

#include "BulletManager.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include "Fade.h"
#include "GPUParticleManager.h"
#include "GameDataManager.h"
#include "GameSettingsManager.h"
#include "GameRule.h"
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
#include "VFXSequencer.h"
#include "WinApp.h"
#include "engine/utility/math/AnimationInterpolation.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr const char* kGoalCrownSparklePreset = "crown_goal_idle_sparkle";

struct GoalCrownPhysicsTuning {
    float positionStiffness = 76.0f;
    float positionDamping = 8.8f;
    float angularStiffness = 52.0f;
    float angularDamping = 7.4f;
    float maxPlanarOffset = 0.32f;
    float maxSink = 0.18f;
    float maxLift = 0.23f;
    float tiltFromOffset = 1.65f;
    float tiltFromVelocity = 0.045f;
    float bodyTiltFollow = 0.34f;
    float landingDownVelocity = -1.15f;
    float landingRollVelocity = 2.20f;
    float compressionScale = 0.14f;
};

GoalCrownPhysicsTuning& GetGoalCrownPhysicsTuning() {
    static GoalCrownPhysicsTuning tuning;
    return tuning;
}

#ifdef USE_IMGUI
void DrawGoalCrownPhysicsTuning() {
    GoalCrownPhysicsTuning& tuning = GetGoalCrownPhysicsTuning();
    if (ImGui::Begin("ゴール王冠・疑似物理調整")) {
        ImGui::TextUnformatted("位置バネ");
        ImGui::SliderFloat("硬さ##CrownPosition", &tuning.positionStiffness, 20.0f, 180.0f, "%.1f");
        ImGui::SliderFloat("減衰##CrownPosition", &tuning.positionDamping, 2.0f, 24.0f, "%.2f");
        ImGui::SliderFloat("横ずれ上限", &tuning.maxPlanarOffset, 0.05f, 0.60f, "%.3f");
        ImGui::SliderFloat("沈み上限", &tuning.maxSink, 0.02f, 0.35f, "%.3f");
        ImGui::SliderFloat("浮き上限", &tuning.maxLift, 0.02f, 0.45f, "%.3f");

        ImGui::TextUnformatted("傾きバネ");
        ImGui::SliderFloat("硬さ##CrownRotation", &tuning.angularStiffness, 10.0f, 140.0f, "%.1f");
        ImGui::SliderFloat("減衰##CrownRotation", &tuning.angularDamping, 1.0f, 20.0f, "%.2f");
        ImGui::SliderFloat("ずれから傾く量", &tuning.tiltFromOffset, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("速度から傾く量", &tuning.tiltFromVelocity, 0.0f, 0.12f, "%.3f");
        ImGui::SliderFloat("スライム傾き追従", &tuning.bodyTiltFollow, 0.0f, 1.0f, "%.2f");

        ImGui::TextUnformatted("着地と変形");
        ImGui::SliderFloat("着地下向き速度", &tuning.landingDownVelocity, -3.0f, 0.0f, "%.2f");
        ImGui::SliderFloat("着地回転速度", &tuning.landingRollVelocity, -5.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("王冠の潰れ量", &tuning.compressionScale, 0.0f, 0.30f, "%.2f");

        if (ImGui::Button("既定値に戻す")) {
            tuning = GoalCrownPhysicsTuning{};
        }
    }
    ImGui::End();
}
#endif

Vector3 NormalizePlanarDirection(const Vector3& direction, const Vector3& fallback) {
    Vector3 result = { direction.x, 0.0f, direction.z };
    const float length = std::sqrt(result.x * result.x + result.z * result.z);
    if (length <= 0.0001f) {
        return fallback;
    }
    result.x /= length;
    result.z /= length;
    return result;
}

Vector3 QuadraticBezier(const Vector3& start, const Vector3& control, const Vector3& end, float rate) {
    const float t = std::clamp(rate, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return start * (inv * inv) + control * (2.0f * inv * t) + end * (t * t);
}

Vector3 MakeLookAtEuler(const Vector3& eye, const Vector3& target, float roll = 0.0f) {
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const float dz = target.z - eye.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    return {
        std::atan2(-dy, horizontal),
        std::atan2(dx, dz),
        roll
    };
}

float NormalizeYaw(float yaw) {
    while (yaw > kPi) yaw -= kTwoPi;
    while (yaw < -kPi) yaw += kTwoPi;
    return yaw;
}

}

void GamePlayScene::SetIsGoal(bool isGoal) {
    if (isGoal_ == isGoal) {
        return;
    }

    isGoal_ = isGoal;
    if (!isGoal_) {
        goalCinematicPlayer_.Stop(false);
        Camera* restoreCamera = goalLockedPrimaryCamera_ ? goalLockedPrimaryCamera_ : CameraManager::GetInstance()->GetMainCamera();
        goalClearPlayerAnimator_.RestoreInitialPose();
        if (goalCrownObject_ && goalCrownSnapshotValid_) {
            goalCrownObject_->SetTranslate(goalCrownBasePosition_);
            goalCrownObject_->SetScale(goalCrownBaseScale_);
            goalCrownObject_->SetRotation(goalCrownBaseRotation_);
            goalCrownObject_->UpdateLocalMatrix();
            goalCrownObject_->UpdateWorldMatrix();
        }
        if (player_ && player_->IsCinematicLocked()) {
            player_->EndCinematicLock(true);
        }
        goalSavePerformed_ = false;
        goalPresentationState_ = GoalPresentationState::Inactive;
        goalPresentationTimer_ = 0.0f;
        goalStarEmitTimer_ = 0.0f;
        goalBurstEmitTimer_ = 0.0f;
        goalPlayerSnapshotValid_ = false;
        goalCrownSnapshotValid_ = false;
        goalCrownSpringInitialized_ = false;
        goalCrownSpringPosition_ = {};
        goalCrownSpringVelocity_ = {};
        goalCrownSpringRotation_ = {};
        goalCrownSpringRotationVelocity_ = {};
        goalCameraSnapshotValid_ = false;
        goalReturnFadeStarted_ = false;
        goalLandingCuePlayed_ = false;
        goalResultCuePlayed_ = false;
        goalCrownObject_ = nullptr;
        goalClearPlayerAnimator_.Reset();
        RestoreGoalPresentationCameraInput();
        if (goalPresentationCamera_ &&
            CameraManager::GetInstance()->GetActiveCamera() == goalPresentationCamera_.get()) {
            CameraManager::GetInstance()->SetActiveCamera(nullptr);
        }
        if (restoreCamera) {
            CameraManager::GetInstance()->SetActiveCamera(restoreCamera);
        }
    }
}

void GamePlayScene::StartGoalPresentation(Object3d* crownObject) {
    if (isGoal_ && goalPresentationState_ != GoalPresentationState::Inactive) {
        return;
    }

    if (!player_ || player_->isDead || player_->GetHp() <= 0.0f) {
        return;
    }

    player_->BeginCinematicLock();
    SetIsGoal(true);
    goalPresentationState_ = GoalPresentationState::Celebrating;
    goalPresentationTimer_ = 0.0f;
    goalStarEmitTimer_ = 0.0f;
    goalBurstEmitTimer_ = 0.0f;
    goalReturnFadeStarted_ = false;
    goalLandingCuePlayed_ = false;
    goalResultCuePlayed_ = false;
    goalCrownObject_ = crownObject;
    goalCrownPosition_ = crownObject ? crownObject->GetWorldPosition() : (player_ ? player_->GetWorldPosition() : Vector3{ 0.0f, 0.0f, 0.0f });
    goalCrownSnapshotValid_ = false;
    goalCrownSpringInitialized_ = false;
    goalCrownSpringPosition_ = goalCrownPosition_;
    goalCrownSpringVelocity_ = {};
    goalCrownSpringRotation_ = {};
    goalCrownSpringRotationVelocity_ = {};

    if (crownObject) {
        goalCrownBasePosition_ = crownObject->GetTranslate();
        goalCrownBaseScale_ = crownObject->GetScale();
        goalCrownBaseRotation_ = crownObject->GetRotation();
        goalCrownSnapshotValid_ = true;
    }

    Vector3 preferredForward = { 0.0f, 0.0f, 0.0f };
    if (player_) {
        Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
        if (activeCamera) {
            const Vector3 playerPosition = player_->GetWorldPosition();
            const Vector3 toCamera = {
                activeCamera->GetEye().x - playerPosition.x,
                0.0f,
                activeCamera->GetEye().z - playerPosition.z
            };
            preferredForward = NormalizePlanarDirection(toCamera, preferredForward);
        }
    }

    if (goalClearPlayerAnimator_.Start(player_, goalCrownPosition_, preferredForward)) {
        goalSavedPlayerControlActive_ = goalClearPlayerAnimator_.WasControlActive();
        goalPlayerBasePosition_ = goalClearPlayerAnimator_.GetBasePosition();
        goalPlayerBaseScale_ = goalClearPlayerAnimator_.GetBaseScale();
        goalPlayerBaseRotation_ = goalClearPlayerAnimator_.GetBaseRotation();
        goalPlayerPosePosition_ = goalClearPlayerAnimator_.GetPosePosition();
        goalMoveForward_ = goalClearPlayerAnimator_.GetMoveForward();
        goalMoveRight_ = goalClearPlayerAnimator_.GetMoveRight();
        goalPlayerSnapshotValid_ = true;
    } else {
        goalPlayerSnapshotValid_ = false;
    }
    LockGoalPresentationCameraInput();
    SetupGoalPresentationCamera();

    if (goalCinematicTimelineLoaded_) {
        for (auto& clip : goalCinematicSequence_.animationClips) {
            if (clip.driver == "GoalClearPlayer" && player_) {
                clip.binding.targetName = player_->GetName();
                clip.binding.targetEventId = player_->GetEventID();
            }
        }
        for (auto& track : goalCinematicSequence_.vfxTracks) {
            const bool followsCrown =
                track.sequenceName == "crown_focus_cue" ||
                track.sequenceName == "crown_get_cue";
            Object3d* target = followsCrown ? crownObject : player_;
            if (target) {
                track.binding.targetName = target->GetName();
                track.binding.targetEventId = target->GetEventID();
            }
        }
        goalCinematicPlayer_.SetSequence(&goalCinematicSequence_);
        goalCinematicPlayer_.Play(false);
        goalPresentationTimer_ = goalCinematicPlayer_.GetCurrentTime();
    }

    UpdateGoalPresentationOverlay();
}

void GamePlayScene::Update(float deltaTime) {
    if (HandleGoalClear(deltaTime)) {
        return;
    }

    if (HandleControlsGuideOverlay(deltaTime)) {
        return;
    }

    if (HandlePauseOverlay(deltaTime)) {
        return;
    }

    if (sceneController_) {
        sceneController_->OnUpdate(*this, deltaTime);
    }

    UpdatePostEffectState(deltaTime);
    if (lifeLostPresentationActive_) {
        UpdateUI(deltaTime);
        return;
    }

    UpdateLockOnAndCamera(deltaTime);
    UpdateGoalCrownIdleAnimation(deltaTime);
    UpdateSceneSystems(deltaTime);
    UpdateUI(deltaTime);
    UpdateEffectDebugShortcuts();

    if (animatedCube_) {
        animatedCube_->Update(deltaTime);
    }
}

void GamePlayScene::UpdateGoalCrownIdleAnimation(float deltaTime) {
    if (!objectManager_ || isGoal_) {
        return;
    }

    constexpr float kRotationSpeed = 0.62f;
    constexpr float kBobAngularSpeed = 1.75f;
    constexpr float kBobAmplitude = 0.14f;
    constexpr float kSparkleInterval = 0.28f;
    constexpr std::array<Vector3, 8> kSparkleOffsets = {
        Vector3{ 0.00f, 0.52f, -0.05f },
        Vector3{ -0.58f, 0.39f, -0.08f },
        Vector3{ 0.58f, 0.39f, -0.08f },
        Vector3{ -0.92f, 0.05f, -0.10f },
        Vector3{ 0.92f, 0.05f, -0.10f },
        Vector3{ -0.70f, -0.20f, 0.16f },
        Vector3{ 0.70f, -0.20f, 0.16f },
        Vector3{ 0.00f, 0.28f, 0.58f }
    };

    const float previousTime = goalCrownIdleTime_;
    goalCrownIdleTime_ += deltaTime;
    goalCrownSparkleTimer_ -= deltaTime;

    const float previousBob = std::sin(previousTime * kBobAngularSpeed) * kBobAmplitude;
    const float currentBob = std::sin(goalCrownIdleTime_ * kBobAngularSpeed) * kBobAmplitude;
    const float bobDelta = currentBob - previousBob;
    const bool emitSparkle = goalCrownSparkleTimer_ <= 0.0f;
    const float emissivePulse = (std::sin(goalCrownIdleTime_ * 2.4f) + 1.0f) * 0.5f;
    const bool isCleared = GameDataManager::GetInstance()->IsStageCleared(
        StageManager::GetInstance()->GetCurrentStageIndex());
    bool foundGoalCrown = false;

    for (auto& object : objectManager_->GetObjects()) {
        if (!object || object->GetEventType() != EventType::Goal) {
            continue;
        }
        foundGoalCrown = true;

        Vector3 position = object->GetTranslate();
        position.y += bobDelta;
        object->SetTranslate(position);

        Vector3 rotation = object->GetRotation();
        rotation.y += kRotationSpeed * deltaTime;
        object->SetRotation(rotation);
        object->SetEmissive(isCleared
            ? 1.20f + emissivePulse * 0.38f
            : 1.65f + emissivePulse * 0.62f);
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();

        const Matrix4x4& crownWorld = object->GetWorldMatrix();
        if (emitSparkle) {
            const Vector3& localOffset = kSparkleOffsets[goalCrownSparklePatternIndex_ % kSparkleOffsets.size()];
            GPUParticleManager::GetInstance()->Emit(kGoalCrownSparklePreset, Math::Transform(localOffset, crownWorld));
        }
    }

    if (!foundGoalCrown) {
        return;
    }
    if (emitSparkle) {
        goalCrownSparkleTimer_ += kSparkleInterval;
        ++goalCrownSparklePatternIndex_;
    }
}

bool GamePlayScene::HandleControlsGuideOverlay(float deltaTime) {
    if (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) {
        controlsGuideOverlay_->SetPlayer(player_);
        controlsGuideOverlay_->Update(deltaTime);
        return true;
    }

    if (IsControlsGuideOpenTriggered()) {
        if (controlsGuideOverlay_) {
            controlsGuideOverlay_->SetPlayer(player_);
            controlsGuideOverlay_->SetActive(true);
        }
        return true;
    }

    return false;
}

bool GamePlayScene::IsControlsGuideOpenTriggered() const {
    if (!inputManager_) {
        return false;
    }

#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard || io.WantTextInput) {
        return false;
    }
#endif

    if (lifeLostPresentationActive_ || lifeLostBlackHold_ || isGoal_ ||
        (pauseMenuOverlay_ && pauseMenuOverlay_->IsActive()) ||
        (settingsOverlay_ && settingsOverlay_->IsActive())) {
        return false;
    }

    return inputManager_->IsKeyTriggered(DIK_TAB);
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
        std::vector<int> newStarCoinIndices;
        for (int i = 0; i < 3; i++) {
            if (sessionStarCoins_[i] && !gameData->IsStarCoinCollected(currentStage, i)) {
                newStarCoinIndices.push_back(i);
            }
        }

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
        gameData->RequestStageSelectReturnPresentation(currentStage, previousCrownCount, newCrownCount, newStarCoinIndices);

        if (saveIndicatorOverlay_) {
            saveIndicatorOverlay_->Play(1.35f);
        }
        DebugConsole::GetInstance()->AddLog("Saving stage clear data...");
        goalSavePerformed_ = true;
    }

    UpdateGoalPresentation(deltaTime);
    UpdatePostEffectState(deltaTime);
    if (particleSystem_) {
        particleSystem_->Update(deltaTime);
    }
    GPUParticleManager::GetInstance()->Update(deltaTime);
    UpdateUI(deltaTime);

    if (!goalEditorPreviewMode_ && goalPresentationState_ == GoalPresentationState::ReadyToReturn) {
        constexpr float kAutomaticReturnDelay = 0.85f;
        const bool confirmPressed = inputManager_ && inputManager_->IsActionTriggered("Jump");
        const bool automaticReturn =
            goalPresentationTimer_ >= goalClearPlayerAnimator_.GetTuning().readyTime + kAutomaticReturnDelay;
        if (confirmPressed || automaticReturn) {
            RequestGoalReturnToSelect();
        }
    }

    if (goalPresentationState_ == GoalPresentationState::Returning) {
        if (!goalReturnFadeStarted_ || Fade::GetInstance()->IsFinished()) {
            SceneManager::GetInstance()->ChangeScene("SELECT");
        }
        return true;
    }

    deltaTime = 0.0f;
    return true;
}

void GamePlayScene::UpdateGoalPresentation(float deltaTime) {
    if (goalPresentationState_ == GoalPresentationState::Inactive ||
        goalPresentationState_ == GoalPresentationState::Returning) {
        return;
    }

    if (goalCinematicTimelineLoaded_) {
        const bool wasTimelinePlaying = goalCinematicPlayer_.IsPlaying();
        goalCinematicPlayer_.Update(deltaTime);
        if (wasTimelinePlaying) {
            goalPresentationTimer_ = goalCinematicPlayer_.GetCurrentTime();
        } else {
            // Ready後もUIの点滅と王冠のばね物理は動かし続けます。
            goalPresentationTimer_ += deltaTime;
        }
    } else {
        goalPresentationTimer_ += deltaTime;
    }
    UpdateGoalPlayerCelebration(deltaTime);
    UpdateGoalCrownMotion(deltaTime);
    UpdateGoalPresentationCamera();
    if (!goalCinematicTimelineLoaded_) {
        EmitGoalPresentationEffects(deltaTime);
    }
    UpdateGoalPresentationOverlay();

    if (goalPresentationState_ == GoalPresentationState::Celebrating &&
        goalPresentationTimer_ >= goalClearPlayerAnimator_.GetTuning().readyTime) {
        goalPresentationState_ = GoalPresentationState::ReadyToReturn;
    }
}

void GamePlayScene::UpdateGoalPlayerCelebration(float deltaTime) {
    (void)deltaTime;
    if (!player_ || !goalPlayerSnapshotValid_) {
        return;
    }

    if (!goalCinematicTimelineLoaded_) {
        goalClearPlayerAnimator_.Update(goalPresentationTimer_);
        goalPlayerPosePosition_ = goalClearPlayerAnimator_.GetPosePosition();
    }
}

void GamePlayScene::UpdateGoalCrownMotion(float deltaTime) {
    if (!goalCrownObject_ || !goalCrownSnapshotValid_ || !goalPlayerSnapshotValid_) {
        return;
    }

#ifdef USE_IMGUI
    DrawGoalCrownPhysicsTuning();
#endif

    const GoalCrownPhysicsTuning& physics = GetGoalCrownPhysicsTuning();
    const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();
    const GoalPresentationTuning& presentation = goalPresentationTuning_;
    const float t = goalPresentationTimer_;
    const Vector3 playerScale = goalClearPlayerAnimator_.GetCurrentScale();
    const Vector3 playerRotation = player_ ? player_->GetRotation() : goalPlayerBaseRotation_;
    const float baseScaleY = std::max(std::abs(goalPlayerBaseScale_.y), 0.001f);
    const float squashRate = std::clamp((goalPlayerBaseScale_.y - playerScale.y) / baseScaleY, -0.35f, 0.35f);
    const float stretchRate = std::max(0.0f, -squashRate);
    const float slimeSurfaceHeight = std::max(0.82f, playerScale.y * 0.72f + 0.30f);

    float crownPivotFromBottom = 0.0f;
    if (Model* crownModel = goalCrownObject_->GetModel()) {
        crownPivotFromBottom = std::max(0.0f, -crownModel->GetLocalAabbMin().y * std::abs(goalCrownBaseScale_.y));
    }
    const float seatDepth = presentation.crownSeatDepth
        + std::max(0.0f, squashRate) * 0.10f
        - stretchRate * 0.035f;
    const float crownPivotHeight = std::max(0.72f, slimeSurfaceHeight - seatDepth + crownPivotFromBottom);
    const Matrix4x4 playerRotationMatrix = Math::MakeRotateMatrix(playerRotation);
    const Vector3 headOffset = Math::TransformNormal({ 0.0f, crownPivotHeight, 0.0f }, playerRotationMatrix);
    const Vector3 headPosition = goalPlayerPosePosition_ + headOffset;
    const Vector3 dropStartPosition = headPosition + Vector3{ 0.0f, presentation.crownDropHeight, 0.0f };

    Vector3 crownPosition = goalCrownBasePosition_;
    Vector3 crownScale = goalCrownBaseScale_;
    Vector3 crownRotation = goalCrownBaseRotation_;

    if (t < presentation.crownMoveStartTime) {
        // カメラが寄り切るまでは王冠をその場に留め、取得物を読ませます。
        const float focusPulse = (std::sin(t * 7.0f) + 1.0f) * 0.5f;
        const float focusScale = 1.0f + focusPulse * 0.012f;
        crownScale.x *= focusScale;
        crownScale.y *= focusScale;
        crownScale.z *= focusScale;
        crownRotation.y = goalCrownBaseRotation_.y + t * 0.34f;
    } else if (t < animation.crownLandTime) {
        const float moveDuration = std::max(0.01f, animation.crownLandTime - presentation.crownMoveStartTime);
        const float approachEndTime = presentation.crownMoveStartTime + moveDuration * 0.34f;
        const float moveStartYaw = goalCrownBaseRotation_.y + presentation.crownMoveStartTime * 0.34f;

        if (t < approachEndTime) {
            // 取得位置から頭上へ弧を描いて運び、そこから降下へ接続します。
            const float approach = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(t, presentation.crownMoveStartTime, approachEndTime),
                AnimationInterpolation::EasingType::SmootherStep);
            Vector3 control = AnimationInterpolation::Lerp(goalCrownBasePosition_, dropStartPosition, 0.52f);
            control.y = std::max(goalCrownBasePosition_.y, dropStartPosition.y) + presentation.crownDropHeight * 0.34f;
            control = control + goalMoveRight_ * 0.28f;
            crownPosition = QuadraticBezier(goalCrownBasePosition_, control, dropStartPosition, approach);
            crownRotation = AnimationInterpolation::SlerpEuler(
                { goalCrownBaseRotation_.x, moveStartYaw, goalCrownBaseRotation_.z },
                { goalCrownBaseRotation_.x - 0.06f, goalCrownBaseRotation_.y + 0.38f, goalCrownBaseRotation_.z + 0.05f },
                approach);
            const float arcPulse = std::sin(approach * kPi);
            crownScale.x *= 1.0f + arcPulse * 0.018f;
            crownScale.y *= 1.0f + arcPulse * 0.012f;
            crownScale.z *= 1.0f + arcPulse * 0.018f;
        } else {
            const float fall = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(t, approachEndTime, animation.crownLandTime),
                AnimationInterpolation::EasingType::SmootherStep);
            crownPosition = AnimationInterpolation::Lerp(dropStartPosition, headPosition, fall);
            crownRotation = AnimationInterpolation::SlerpEuler(
                { goalCrownBaseRotation_.x - 0.06f, goalCrownBaseRotation_.y + 0.38f, goalCrownBaseRotation_.z + 0.05f },
                { goalCrownBaseRotation_.x, goalCrownBaseRotation_.y + 0.52f, goalCrownBaseRotation_.z },
                fall);
            const float descentPulse = std::sin(fall * kPi);
            crownScale.x *= 1.0f + descentPulse * 0.016f;
            crownScale.y *= 1.0f + descentPulse * 0.010f;
            crownScale.z *= 1.0f + descentPulse * 0.016f;
        }
        goalCrownSpringInitialized_ = false;
        goalCrownSpringPosition_ = crownPosition;
        goalCrownSpringVelocity_ = {};
        goalCrownSpringRotation_ = crownRotation;
        goalCrownSpringRotationVelocity_ = {};
    } else {
        Vector3 targetPosition = headPosition + goalMoveForward_ * (squashRate * 0.055f);
        float motionEnergy = 0.0f;
        if (t >= animation.jumpStartTime && t < animation.apexTime) {
            const float jumpRate = AnimationInterpolation::SegmentRate(t, animation.jumpStartTime, animation.apexTime);
            motionEnergy = std::sin(jumpRate * kPi);
        }

        if (!goalCrownSpringInitialized_) {
            goalCrownSpringPosition_ = targetPosition + Vector3{ 0.0f, -0.08f, 0.0f };
            goalCrownSpringVelocity_ = goalMoveForward_ * -0.18f + Vector3{ 0.0f, physics.landingDownVelocity, 0.0f };
            goalCrownSpringRotation_ = {
                goalCrownBaseRotation_.x + 0.10f,
                goalCrownBaseRotation_.y + 0.52f,
                goalCrownBaseRotation_.z - 0.12f
            };
            goalCrownSpringRotationVelocity_ = { 0.0f, 0.0f, physics.landingRollVelocity };
            goalCrownSpringInitialized_ = true;
        }

        const float dt = std::clamp(deltaTime, 0.0f, 1.0f / 30.0f);
        const int substepCount = std::max(1, static_cast<int>(std::ceil(dt / (1.0f / 120.0f))));
        const float substep = dt / static_cast<float>(substepCount);
        for (int step = 0; step < substepCount; ++step) {
            const Vector3 springDelta = targetPosition - goalCrownSpringPosition_;
            const Vector3 acceleration = springDelta * (physics.positionStiffness + motionEnergy * 8.0f)
                - goalCrownSpringVelocity_ * physics.positionDamping;
            goalCrownSpringVelocity_ = goalCrownSpringVelocity_ + acceleration * substep;
            goalCrownSpringPosition_ = goalCrownSpringPosition_ + goalCrownSpringVelocity_ * substep;
        }

        Vector3 crownOffset = goalCrownSpringPosition_ - targetPosition;
        const float maxPlanarOffset = physics.maxPlanarOffset + motionEnergy * 0.05f;
        const float planarLength = std::sqrt(crownOffset.x * crownOffset.x + crownOffset.z * crownOffset.z);
        if (planarLength > maxPlanarOffset && planarLength > 0.0001f) {
            const float rate = maxPlanarOffset / planarLength;
            crownOffset.x *= rate;
            crownOffset.z *= rate;
            goalCrownSpringVelocity_.x *= 0.55f;
            goalCrownSpringVelocity_.z *= 0.55f;
        }
        const float unclampedY = crownOffset.y;
        crownOffset.y = std::clamp(crownOffset.y, -physics.maxSink, physics.maxLift + motionEnergy * 0.04f);
        if (crownOffset.y != unclampedY) {
            goalCrownSpringVelocity_.y *= 0.32f;
        }
        crownPosition = targetPosition + crownOffset;
        goalCrownSpringPosition_ = crownPosition;

        const float forwardOffset = Math::Dot(crownOffset, goalMoveForward_);
        const float rightOffset = Math::Dot(crownOffset, goalMoveRight_);
        const float forwardSpeed = Math::Dot(goalCrownSpringVelocity_, goalMoveForward_);
        const float rightSpeed = Math::Dot(goalCrownSpringVelocity_, goalMoveRight_);
        Vector3 targetRotation = goalCrownBaseRotation_;
        targetRotation.x += (playerRotation.x - goalPlayerBaseRotation_.x) * physics.bodyTiltFollow
            - forwardOffset * physics.tiltFromOffset
            - forwardSpeed * physics.tiltFromVelocity;
        targetRotation.z += (playerRotation.z - goalPlayerBaseRotation_.z) * physics.bodyTiltFollow
            + rightOffset * physics.tiltFromOffset
            + rightSpeed * physics.tiltFromVelocity;
        targetRotation.x = std::clamp(targetRotation.x, goalCrownBaseRotation_.x - 0.52f, goalCrownBaseRotation_.x + 0.52f);
        targetRotation.z = std::clamp(targetRotation.z, goalCrownBaseRotation_.z - 0.52f, goalCrownBaseRotation_.z + 0.52f);
        const float targetYaw = goalCrownBaseRotation_.y + NormalizeYaw(playerRotation.y - goalPlayerBaseRotation_.y);
        targetRotation.y = goalCrownSpringRotation_.y + NormalizeYaw(targetYaw - goalCrownSpringRotation_.y);

        for (int step = 0; step < substepCount; ++step) {
            Vector3 rotationDelta = targetRotation - goalCrownSpringRotation_;
            rotationDelta.y = NormalizeYaw(rotationDelta.y);
            const Vector3 acceleration = rotationDelta * physics.angularStiffness
                - goalCrownSpringRotationVelocity_ * physics.angularDamping;
            goalCrownSpringRotationVelocity_ = goalCrownSpringRotationVelocity_ + acceleration * substep;
            goalCrownSpringRotation_ = goalCrownSpringRotation_ + goalCrownSpringRotationVelocity_ * substep;
        }
        crownRotation = goalCrownSpringRotation_;

        const float compression = std::clamp(
            -crownOffset.y * 2.2f + std::max(0.0f, -goalCrownSpringVelocity_.y) * 0.045f,
            0.0f,
            0.35f);
        crownScale.x *= 1.0f + compression * physics.compressionScale;
        crownScale.y *= 1.0f - compression * physics.compressionScale * 0.72f;
        crownScale.z *= 1.0f + compression * physics.compressionScale;
    }

    goalCrownPosition_ = crownPosition;
    goalCrownObject_->SetTranslate(crownPosition);
    goalCrownObject_->SetScale(crownScale);
    goalCrownObject_->SetRotation(crownRotation);
    goalCrownObject_->UpdateLocalMatrix();
    goalCrownObject_->UpdateWorldMatrix();
}

void GamePlayScene::EmitGoalPresentationEffects(float deltaTime) {
    (void)deltaTime;
    const float t = goalPresentationTimer_;
    const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();

    if (!goalLandingCuePlayed_ && t >= animation.crownLandTime) {
        const float crownScale = std::clamp(
            std::max({ std::abs(goalCrownBaseScale_.x), std::abs(goalCrownBaseScale_.y), std::abs(goalCrownBaseScale_.z) }) * 0.72f,
            0.80f,
            1.80f);
        VFXSequencer::PlayOneShot(
            "crown_get_cue",
            goalCrownPosition_,
            { crownScale, crownScale, crownScale });
        goalLandingCuePlayed_ = true;
    }

    if (!goalResultCuePlayed_ && t >= animation.apexTime) {
        Vector3 resultPosition = goalPlayerPosePosition_;
        resultPosition.y += std::max(1.25f, goalClearPlayerAnimator_.GetCurrentScale().y * 0.50f);
        VFXSequencer::PlayOneShot("crown_result_cue", resultPosition);
        goalResultCuePlayed_ = true;
    }
}

void GamePlayScene::SetupGoalPresentationCamera() {
    Camera* sourceCamera = goalLockedPrimaryCamera_;
    if (!sourceCamera) {
        sourceCamera = CameraManager::GetInstance()->GetActiveCamera();
    }
    if (!sourceCamera) {
        sourceCamera = CameraManager::GetInstance()->GetMainCamera();
    }

    const GoalPresentationTuning& camera = goalPresentationTuning_;
    const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();
    goalCameraGameplayEye_ = sourceCamera ? sourceCamera->GetEye() : goalPlayerBasePosition_ + Vector3{ 0.0f, 3.0f, 8.0f };
    goalCameraGameplayTarget_ = sourceCamera ? sourceCamera->GetTargetPoint() : goalPlayerBasePosition_ + Vector3{ 0.0f, 1.0f, 0.0f };
    goalCameraGameplayFov_ = sourceCamera ? sourceCamera->GetFovY() : camera.landingFov;

    goalCameraFocusTarget_ = goalCrownPosition_ + Vector3{ 0.0f, 0.18f, 0.0f };
    goalCameraFocusEye_ = goalCameraFocusTarget_
        + goalMoveForward_ * camera.crownFocusDistance
        + goalMoveRight_ * camera.crownFocusSide
        + Vector3{ 0.0f, camera.crownFocusHeight, 0.0f };

    const Vector3 landingPlayerTarget = goalPlayerBasePosition_ + Vector3{ 0.0f, std::max(1.15f, goalPlayerBaseScale_.y * 0.62f), 0.0f };
    goalCameraLandingTarget_ = landingPlayerTarget;
    goalCameraLandingEye_ = landingPlayerTarget
        + goalMoveForward_ * camera.landingCameraDistance
        + goalMoveRight_ * camera.landingCameraSide
        + Vector3{ 0.0f, camera.landingCameraHeight, 0.0f };

    const float anticipationScaleY = goalPlayerBaseScale_.y * (1.0f - animation.anticipationSquash * 0.92f);
    goalCameraJumpTarget_ = goalPlayerBasePosition_ + Vector3{
        0.0f,
        -animation.anticipationDepth + std::max(0.82f, anticipationScaleY * 0.46f),
        0.0f
    };
    goalCameraJumpEye_ = goalCameraJumpTarget_
        + goalMoveForward_ * camera.jumpCameraDistance
        + goalMoveRight_ * camera.jumpCameraSide
        + Vector3{ 0.0f, camera.jumpCameraHeight, 0.0f };

    const Vector3 finalPosePosition = goalPlayerBasePosition_
        + goalMoveForward_ * animation.forwardDistance
        + Vector3{ 0.0f, animation.jumpHeight, 0.0f };
    goalCameraResultTarget_ = finalPosePosition
        + goalMoveRight_ * camera.resultTargetSide
        + Vector3{ 0.0f, std::max(0.72f, goalPlayerBaseScale_.y * 0.46f), 0.0f };
    goalCameraResultEye_ = goalCameraResultTarget_
        + goalMoveForward_ * camera.resultCameraDistance
        + goalMoveRight_ * camera.resultCameraSide
        + Vector3{ 0.0f, camera.resultCameraHeight, 0.0f };

    goalPresentationCamera_ = std::make_unique<Camera>();
    goalPresentationCamera_->Initialize();
    goalPresentationCamera_->SetInputEnabled(false);
    goalPresentationCamera_->SetFollowTarget(nullptr);
    goalPresentationCamera_->SetLockOnTarget(nullptr);
    goalPresentationCamera_->SetFollowMode(Camera::FollowMode::kFixedPoint);
    goalPresentationCamera_->SetFovY(goalCameraGameplayFov_);
    goalPresentationCamera_->SetEye(goalCameraGameplayEye_);
    goalPresentationCamera_->SetTarget(goalCameraGameplayTarget_);
    goalPresentationCamera_->SetRotation(MakeLookAtEuler(goalCameraGameplayEye_, goalCameraGameplayTarget_));
    goalPresentationCamera_->ConfigFixedPoint(
        goalCameraGameplayEye_,
        MakeLookAtEuler(goalCameraGameplayEye_, goalCameraGameplayTarget_));
    goalPresentationCamera_->Update();

    goalCameraSnapshotValid_ = true;
    CameraManager::GetInstance()->SetActiveCamera(goalPresentationCamera_.get());
    UpdateGoalPresentationCamera();
}

void GamePlayScene::UpdateGoalPresentationCamera() {
    Camera* presentationCamera = goalPresentationCamera_.get();

    if (!presentationCamera || !goalCameraSnapshotValid_) {
        return;
    }

    CameraManager::GetInstance()->SetActiveCamera(presentationCamera);

    const float t = goalPresentationTimer_;
    const GoalPresentationTuning& camera = goalPresentationTuning_;
    const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();
    Vector3 eye = goalCameraGameplayEye_;
    Vector3 target = goalCameraGameplayTarget_;
    float fov = goalCameraGameplayFov_;

    Vector3 movingCrownTarget = goalCrownPosition_ + Vector3{ 0.0f, 0.18f, 0.0f };
    Vector3 movingCrownEye = movingCrownTarget
        + goalMoveForward_ * camera.crownFocusDistance
        + goalMoveRight_ * camera.crownFocusSide
        + Vector3{ 0.0f, camera.crownFocusHeight, 0.0f };

    if (t < camera.crownFocusEndTime) {
        const float p = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(t, 0.0f, camera.crownFocusEndTime),
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(goalCameraGameplayEye_, movingCrownEye, p);
        target = AnimationInterpolation::Lerp(goalCameraGameplayTarget_, movingCrownTarget, p);
        fov = AnimationInterpolation::Lerp(goalCameraGameplayFov_, camera.crownFocusFov, p);
    } else if (t < animation.crownLandTime) {
        // 王冠の移動中も専用ショットを維持し、常に王冠を画面の主役にします。
        eye = movingCrownEye;
        target = movingCrownTarget;
        fov = camera.crownFocusFov;
    } else if (t < animation.anticipationStartTime) {
        const float p = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(t, animation.crownLandTime, animation.anticipationStartTime),
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(movingCrownEye, goalCameraLandingEye_, p);
        target = AnimationInterpolation::Lerp(movingCrownTarget, goalCameraLandingTarget_, p);
        fov = AnimationInterpolation::Lerp(camera.crownFocusFov, camera.landingFov, p);
    } else if (t < animation.jumpStartTime) {
        const float p = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(t, animation.anticipationStartTime, animation.jumpStartTime),
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(goalCameraLandingEye_, goalCameraJumpEye_, p);
        target = AnimationInterpolation::Lerp(goalCameraLandingTarget_, goalCameraJumpTarget_, p);
        fov = AnimationInterpolation::Lerp(camera.landingFov, camera.jumpFov, p);
    } else if (t < animation.apexTime) {
        const float p = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(t, animation.jumpStartTime, animation.apexTime),
            AnimationInterpolation::EasingType::SmootherStep);
        Vector3 jumpTarget = goalPlayerPosePosition_ + Vector3{ 0.0f, std::max(0.82f, goalClearPlayerAnimator_.GetCurrentScale().y * 0.46f), 0.0f };
        Vector3 jumpEye = jumpTarget
            + goalMoveForward_ * camera.jumpCameraDistance
            + goalMoveRight_ * camera.jumpCameraSide
            + Vector3{ 0.0f, camera.jumpCameraHeight, 0.0f };
        eye = AnimationInterpolation::Lerp(jumpEye, goalCameraResultEye_, p);
        target = AnimationInterpolation::Lerp(jumpTarget, goalCameraResultTarget_, p);
        fov = AnimationInterpolation::Lerp(camera.jumpFov, camera.resultFov, p);
    } else {
        eye = goalCameraResultEye_;
        target = goalCameraResultTarget_;
        fov = camera.resultFov;
    }

    presentationCamera->SetFovY(fov);
    const Vector3 rotation = MakeLookAtEuler(eye, target);
    presentationCamera->SetEye(eye);
    presentationCamera->SetTarget(target);
    presentationCamera->SetRotation(rotation);
    presentationCamera->ConfigFixedPoint(eye, rotation);
    presentationCamera->Update();
}

void GamePlayScene::LockGoalPresentationCameraInput() {
    RestoreGoalPresentationCameraInput();

    CameraManager* cameraManager = CameraManager::GetInstance();
    if (!cameraManager) {
        return;
    }

    goalLockedPrimaryCamera_ = cameraManager->GetActiveCamera();
    if (!goalLockedPrimaryCamera_) {
        goalLockedPrimaryCamera_ = cameraManager->GetMainCamera();
    }
    goalLockedSecondaryCamera_ = cameraManager->GetMainCamera();
    if (goalLockedSecondaryCamera_ == goalLockedPrimaryCamera_) {
        goalLockedSecondaryCamera_ = nullptr;
    }

    if (goalLockedPrimaryCamera_) {
        goalLockedPrimaryCamera_->SetInputEnabled(false);
    }
    if (goalLockedSecondaryCamera_) {
        goalLockedSecondaryCamera_->SetInputEnabled(false);
    }
}

void GamePlayScene::RestoreGoalPresentationCameraInput() {
    if (goalLockedPrimaryCamera_) {
        goalLockedPrimaryCamera_->SetInputEnabled(true);
    }
    if (goalLockedSecondaryCamera_) {
        goalLockedSecondaryCamera_->SetInputEnabled(true);
    }
    goalLockedPrimaryCamera_ = nullptr;
    goalLockedSecondaryCamera_ = nullptr;
}

void GamePlayScene::RequestGoalReturnToSelect() {
    if (goalPresentationState_ == GoalPresentationState::Returning) {
        return;
    }

    goalPresentationState_ = GoalPresentationState::Returning;
    goalCinematicPlayer_.Stop(false);
    goalReturnFadeStarted_ = true;
    Fade::GetInstance()->StartFadeOut(0.55f);
    goalClearPlayerAnimator_.RestoreControl();
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
    CameraManager::GetInstance()->Update(deltaTime);
    particleSystem_->Update(deltaTime);
    objectManager_->Update(deltaTime);
    if (gameRule_) {
        gameRule_->Update(deltaTime);
    }
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

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
#include "game/actor/gimmick/stage/GimmickStageGate.h"
#include "game/actor/gimmick/trigger/GimmickBossGate.h"
#include "engine/utility/math/AnimationInterpolation.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr const char* kGoalCrownSparklePreset = "crown_goal_idle_sparkle";
constexpr const char* kGoalCrownTwinklePreset = "crown_idle_twinkle";
constexpr float kStageEntryCameraIntroDuration = 0.24f;
constexpr float kStageEntryGateOpenLeadTime = 0.42f;
constexpr float kStageEntryCameraRestoreStartTime = 1.62f;
constexpr float kStageEntryCameraRestoreDuration = 1.05f;
constexpr float kStageEntryPresentationDuration =
    kStageEntryCameraRestoreStartTime + kStageEntryCameraRestoreDuration;
constexpr float kStageEntrySetupRetryTimeout = 2.0f;
constexpr float kStageEntryCameraBackDistance = 15.0f;
constexpr float kStageEntryCameraHeight = 3.4f;
constexpr float kStageEntryCameraYawOffset = -0.48f;
constexpr float kStageEntryCameraFocusHeight = 1.15f;
constexpr float kStageEntryCameraLandingFocusRate = 0.72f;
constexpr float kStageEntryThirdPersonDistance = 16.0f;
constexpr float kStageEntryThirdPersonHeight = 3.2f;
constexpr float kStageEntryThirdPersonPitch = 0.32f;
constexpr float kStageEntryCinemaBarHeight = 0.105f;
constexpr float kStageEntryCinemaBarOpenDuration = 0.22f;
constexpr const char* kStageEntryCameraObjectName = "Stage1_EntryCamera";
constexpr const char* kStageEntryCameraFallbackObjectName = "Cinematic_Camera_01";
constexpr float kArenaBossIntroGateShotEnd = 0.74f;
constexpr float kArenaBossIntroGateHoldEnd = 1.05f;
constexpr float kArenaBossIntroBossFocusEnd = 1.55f;
constexpr float kArenaBossIntroBossHoldEnd = 3.55f;
constexpr float kArenaBossIntroDuration = 4.55f;
constexpr float kArenaBossIntroCinemaBarHeight = 0.115f;
constexpr float kArenaBossGameplayCameraDistance = 11.5f;
constexpr float kArenaBossGameplayCameraHeight = 3.8f;
constexpr float kArenaBossGameplayCameraPitch = 0.31415927f;
constexpr float kArenaBossRewardFocusInEnd = 0.32f;
constexpr float kArenaBossRewardTrackEnd = 1.82f;
constexpr float kArenaBossRewardDuration = 2.78f;
constexpr float kArenaBossRewardCinemaBarHeight = 0.115f;

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
        goalWasStageCleared_ = false;
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

    const int currentStage = StageManager::GetInstance()->GetCurrentStageIndex();
    goalWasStageCleared_ = GameDataManager::GetInstance()->IsStageCleared(currentStage);

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
            if (track.sequenceName.rfind("crown_focus", 0) == 0) {
                track.sequenceName = goalWasStageCleared_ ? "crown_focus_silver_cue" : "crown_focus_cue";
            } else if (track.sequenceName.rfind("crown_get", 0) == 0) {
                track.sequenceName = goalWasStageCleared_ ? "crown_get_silver_cue" : "crown_get_cue";
            } else if (track.sequenceName.rfind("crown_result", 0) == 0) {
                track.sequenceName = goalWasStageCleared_ ? "crown_result_silver_cue" : "crown_result_cue";
            } else if (track.sequenceName.rfind("crown_victory_land", 0) == 0) {
                track.sequenceName = goalWasStageCleared_ ? "crown_victory_land_silver_cue" : "crown_victory_land_cue";
            }
            const bool followsCrown =
                track.sequenceName.rfind("crown_focus", 0) == 0 ||
                track.sequenceName.rfind("crown_get", 0) == 0;
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
    bool runtimePlaying = true;
#ifdef USE_IMGUI
    if (SceneManager* sceneManager = GetSceneManager()) {
        runtimePlaying = sceneManager->IsPlaying();
    }
#endif

    if (!runtimePlaying) {
        stageEntryRuntimeWasPlaying_ = false;
        if (stageEntryPresentationActive_) {
            FinishStageEntryPresentation();
        }
    } else {
        // Play In Editor はSceneを再読み込みしないため、停止状態から再生へ変わった瞬間も
        // OnActivatedと同じ入口演出を明示的に要求します。
        if (!stageEntryRuntimeWasPlaying_) {
            stageEntryRuntimeWasPlaying_ = true;
            stageEntryPresentationCompleted_ = false;
            stageEntryHadPlayerControl_ = player_ ? player_->IsControlActive() : true;
            stageEntryPresentationPending_ = true;
            stageEntryPresentationRetryTimer_ = 0.0f;
        }

        if (stageEntryPresentationPending_ &&
            !stageEntryPresentationActive_ &&
            !stageEntryPresentationCompleted_) {
            if (!StartStageEntryPresentation()) {
                stageEntryPresentationRetryTimer_ += std::max(deltaTime, 0.0f);
                if (player_) {
                    player_->SetIsVisible(false);
                    player_->SetIsControlActive(false);
                    player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
                }

                if (stageEntryPresentationRetryTimer_ < kStageEntrySetupRetryTimeout) {
                    UpdatePostEffectState(deltaTime);
                    UpdateUI(deltaTime);
                    return;
                }

                // 入口ゲートを持たない検証Sceneまで停止させないため、一定時間後は通常開始へ戻します。
                stageEntryPresentationPending_ = false;
                stageEntryPresentationCompleted_ = true;
                stageEntryPresentationRetryTimer_ = 0.0f;
                if (player_) {
                    player_->SetIsVisible(true);
                    player_->SetIsControlActive(stageEntryHadPlayerControl_);
                }
                DebugConsole::GetInstance()->AddLog(
                    "Stage Entry Warning: entrance gate presentation could not be prepared.");
            }
        }
    }

    if (stageEntryPresentationActive_) {
        UpdatePostEffectState(deltaTime);
        UpdateStageEntryPresentation(deltaTime);
        UpdateUI(deltaTime);
        return;
    }

    if (HandleGoalClear(deltaTime)) {
        return;
    }

    if (arenaBossIntroActive_) {
        UpdatePostEffectState(deltaTime);
        UpdateArenaBossIntro(deltaTime);
        UpdateSceneSystems(deltaTime);
        UpdateUI(deltaTime);
        return;
    }

    if (arenaBossRewardActive_) {
        UpdatePostEffectState(deltaTime);
        UpdateArenaBossDefeatReward(deltaTime);
        UpdateSceneSystems(deltaTime);
        UpdateUI(deltaTime);
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

Object3d* GamePlayScene::FindStageEntryGate() const {
    if (!objectManager_) {
        return nullptr;
    }

    Object3d* nameMatchedGate = nullptr;
    for (const auto& object : objectManager_->GetObjects()) {
        if (!object || object->GetName().find("_EntranceGate") == std::string::npos) {
            continue;
        }
        if (object->GetGimmickType() == "StageGate") {
            return object.get();
        }
        if (!nameMatchedGate && dynamic_cast<GimmickStageGate*>(object.get())) {
            nameMatchedGate = object.get();
        }
    }
    return nameMatchedGate;
}

bool GamePlayScene::StartStageEntryPresentation() {
    if (stageEntryPresentationActive_) {
        return true;
    }

    stageEntryGate_ = FindStageEntryGate();
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!stageEntryGate_ || !player_ || !camera) {
        stageEntryGate_ = nullptr;
        return false;
    }

    // OnActivated直後は、ロードしたTransformのワールド行列がまだ更新されていない場合があります。
    // 親階層を含めて確定してから入口演出の基準座標を取得します。
    Object3d* gateTransformRoot = stageEntryGate_;
    while (gateTransformRoot->GetParent()) {
        gateTransformRoot = gateTransformRoot->GetParent();
    }
    gateTransformRoot->UpdateWorldMatrix(false);

    // 初期スポーン地点をリスポーン地点として確定させてから、演出用の位置へ移動します。
    player_->Update(0.0f);
    const Vector3 playerSpawn = player_->GetWorldPosition();
    player_->SetRespawnPosition(playerSpawn);
    const Vector3 gatePosition = stageEntryGate_->GetWorldPosition();
    stageEntryDirection_ = NormalizePlanarDirection(playerSpawn - gatePosition, { 1.0f, 0.0f, 0.0f });

    PlayerGateReturnAnimation::Route route;
    route.start = gatePosition - stageEntryDirection_ * 1.38f;
    route.gateCenter = gatePosition;
    route.end = playerSpawn;
    route.direction = stageEntryDirection_;
    route.baseScale = player_->GetScale();
    player_->StartGateReturnAnimation(route);
    // ゲートの反応を先に見せ、内部から出始める瞬間までプレイヤーを隠します。
    player_->SetIsVisible(false);
    stageEntryPlayerEmergenceStarted_ = false;

    if (auto* gate = dynamic_cast<GimmickStageGate*>(stageEntryGate_)) {
        // 登場中に入口ゲートへ触れて、即座にステージ選択へ戻らないようにします。
        gate->SetTransitionEnabled(false);
        gate->TriggerEntryReaction();
    }

    if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
        stageEntryCinemaBarBaseHeight_ = params->cinemaBarHeight;
        stageEntryCinemaBarOverrideActive_ = true;
    }

    const Vector3 gateFocus = {
        gatePosition.x,
        playerSpawn.y + kStageEntryCameraFocusHeight,
        gatePosition.z
    };
    const Vector3 landingFocus = playerSpawn + Vector3{ 0.0f, kStageEntryCameraFocusHeight, 0.0f };
    stageEntryCameraFocusTarget_ = AnimationInterpolation::Lerp(
        gateFocus, landingFocus, kStageEntryCameraLandingFocusRate);
    // ステージ内部の中庭へ直進すると地形内へ入るため、入口の外側から斜めに見ます。
    const float thirdPersonYaw =
        std::atan2(stageEntryDirection_.x, stageEntryDirection_.z)
        + kStageEntryCameraYawOffset;
    const Matrix4x4 cinematicYawRotation = Math::MakeRotateYMatrix(thirdPersonYaw);
    stageEntryCameraFocusEye_ = stageEntryCameraFocusTarget_
        + Math::TransformNormal(
            { 0.0f, 0.0f, -kStageEntryCameraBackDistance },
            cinematicYawRotation)
        + Vector3{ 0.0f, kStageEntryCameraHeight, 0.0f };
    // 編集中の自由カメラ位置を引き継がず、毎回ゲートを読める決定的な導入ショットから始めます。
    const Vector3 cinematicBackDirection = NormalizePlanarDirection(
        stageEntryCameraFocusEye_ - stageEntryCameraFocusTarget_,
        { -stageEntryDirection_.x, 0.0f, -stageEntryDirection_.z });
    stageEntryCameraStartTarget_ = gateFocus;
    stageEntryCameraStartEye_ = stageEntryCameraFocusEye_
        + cinematicBackDirection * 2.0f
        + Vector3{ 0.0f, 0.6f, 0.0f };
    // 復帰先はSnapToThirdPersonと同じ式で作り、演出終了フレームの段差をなくします。
    const Matrix4x4 thirdPersonRotation =
        Math::MakeRotateXMatrix(kStageEntryThirdPersonPitch)
        * Math::MakeRotateYMatrix(thirdPersonYaw);
    stageEntryCameraRestoreTarget_ = playerSpawn
        + Vector3{ 0.0f, kStageEntryThirdPersonHeight, 0.0f };
    stageEntryCameraRestoreEye_ = stageEntryCameraRestoreTarget_
        + Math::TransformNormal(
            { 0.0f, 0.0f, -kStageEntryThirdPersonDistance },
            thirdPersonRotation);

    // 入口専用名を最優先し、旧データでは入口ゲートに最も近い汎用カメラを採用します。
    // 名前を分けることで、JSON読込時に同名Objectとして上書きされることも防ぎます。
    Object3d* authoredEntryCamera = nullptr;
    Object3d* fallbackEntryCamera = nullptr;
    float nearestCameraDistanceSq = (std::numeric_limits<float>::max)();
    if (objectManager_) {
        for (const auto& object : objectManager_->GetObjects()) {
            if (!object || !object->IsCameraObject()) {
                continue;
            }
            if (object->GetName() == kStageEntryCameraObjectName) {
                authoredEntryCamera = object.get();
                break;
            }
            if (object->GetName() != kStageEntryCameraFallbackObjectName) {
                continue;
            }
            Object3d* candidateTransformRoot = object.get();
            while (candidateTransformRoot->GetParent()) {
                candidateTransformRoot = candidateTransformRoot->GetParent();
            }
            candidateTransformRoot->UpdateWorldMatrix(false);
            const Vector3 offset = object->GetWorldPosition() - gatePosition;
            const float distanceSq = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
            if (distanceSq < nearestCameraDistanceSq) {
                nearestCameraDistanceSq = distanceSq;
                fallbackEntryCamera = object.get();
            }
        }
    }
    if (!authoredEntryCamera) {
        authoredEntryCamera = fallbackEntryCamera;
    }

    stageEntryCameraBaseFov_ = camera->GetFovY();
    stageEntryCameraFocusFov_ = stageEntryCameraBaseFov_;
    if (authoredEntryCamera) {
        Object3d* cameraTransformRoot = authoredEntryCamera;
        while (cameraTransformRoot->GetParent()) {
            cameraTransformRoot = cameraTransformRoot->GetParent();
        }
        cameraTransformRoot->UpdateWorldMatrix(false);

        Vector3 authoredEye{};
        Vector3 authoredTarget{};
        if (CameraEditor::GetInstance()->ResolveSceneCameraPose(
                authoredEntryCamera, authoredEye, authoredTarget)) {
            stageEntryCameraStartEye_ = authoredEye;
            stageEntryCameraStartTarget_ = authoredTarget;
            stageEntryCameraFocusEye_ = authoredEye;
            stageEntryCameraFocusTarget_ = authoredTarget;
            stageEntryCameraFocusFov_ = authoredEntryCamera->GetSceneCameraSettings().fovY;
            DebugConsole::GetInstance()->AddLog(
                "Stage Entry: using authored camera for Stage1_EntranceGate.");
        }
    }
    // Game Viewが別の一時カメラを参照していても、入口演出中は必ずこのメインカメラを描画します。
    CameraManager::GetInstance()->SetActiveCamera(camera);
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetLockOnTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(stageEntryCameraStartEye_);
    camera->SetTarget(stageEntryCameraStartTarget_);
    const Vector3 startRotation = MakeLookAtEuler(
        stageEntryCameraStartEye_, stageEntryCameraStartTarget_);
    camera->SetRotation(startRotation);
    camera->ConfigFixedPoint(stageEntryCameraStartEye_, startRotation);
    camera->SetFovY(stageEntryCameraFocusFov_);
    camera->Update(0.0f);

    stageEntryPresentationTimer_ = 0.0f;
    stageEntryPresentationRetryTimer_ = 0.0f;
    stageEntryPresentationPending_ = false;
    stageEntryPresentationActive_ = true;
    DebugConsole::GetInstance()->AddLog("Stage Entry: gate presentation start.");
    return true;
}

void GamePlayScene::UpdateStageEntryPresentation(float deltaTime) {
    if (!stageEntryPresentationActive_ || !player_) {
        FinishStageEntryPresentation();
        return;
    }

    stageEntryPresentationTimer_ += std::max(deltaTime, 0.0f);
    if (!stageEntryPlayerEmergenceStarted_
        && stageEntryPresentationTimer_ >= kStageEntryGateOpenLeadTime) {
        stageEntryPlayerEmergenceStarted_ = true;
        player_->SetIsVisible(true);
    }
    if (stageEntryPlayerEmergenceStarted_) {
        player_->Update(deltaTime);
    } else {
        player_->SetIsVisible(false);
        player_->SetIsControlActive(false);
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
    }
    if (stageEntryGate_) {
        stageEntryGate_->Update(deltaTime);
    }

    if (stageEntryCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            const float openRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(
                    stageEntryPresentationTimer_, 0.0f, kStageEntryCinemaBarOpenDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            const float closeRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(
                    stageEntryPresentationTimer_,
                    kStageEntryCameraRestoreStartTime,
                    kStageEntryPresentationDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            const float blend = openRate * (1.0f - closeRate);
            const float targetHeight = std::max(stageEntryCinemaBarBaseHeight_, kStageEntryCinemaBarHeight);
            params->cinemaBarHeight = stageEntryCinemaBarBaseHeight_
                + (targetHeight - stageEntryCinemaBarBaseHeight_) * blend;
        }
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera) {
        CameraManager::GetInstance()->SetActiveCamera(camera);
        Vector3 eye = stageEntryCameraFocusEye_;
        Vector3 target = stageEntryCameraFocusTarget_;
        float fov = stageEntryCameraFocusFov_;
        if (stageEntryPresentationTimer_ < kStageEntryCameraRestoreStartTime) {
            const float introRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(
                    stageEntryPresentationTimer_, 0.0f, kStageEntryCameraIntroDuration),
                AnimationInterpolation::EasingType::EaseOut);
            eye = AnimationInterpolation::Lerp(stageEntryCameraStartEye_, stageEntryCameraFocusEye_, introRate);
            target = AnimationInterpolation::Lerp(stageEntryCameraStartTarget_, stageEntryCameraFocusTarget_, introRate);
        } else {
            const float restoreRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(
                    stageEntryPresentationTimer_,
                    kStageEntryCameraRestoreStartTime,
                    kStageEntryPresentationDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            eye = AnimationInterpolation::Lerp(stageEntryCameraFocusEye_, stageEntryCameraRestoreEye_, restoreRate);
            target = AnimationInterpolation::Lerp(stageEntryCameraFocusTarget_, stageEntryCameraRestoreTarget_, restoreRate);
            fov = AnimationInterpolation::Lerp(
                stageEntryCameraFocusFov_, stageEntryCameraBaseFov_, restoreRate);
        }

        camera->SetInputEnabled(false);
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
        // 追従対象がない演出カメラでは、固定点設定ではなく実際の視点を直接更新します。
        camera->SetEye(eye);
        camera->SetTarget(target);
        const Vector3 rotation = MakeLookAtEuler(eye, target);
        camera->SetRotation(rotation);
        camera->ConfigFixedPoint(eye, rotation);
        camera->SetFovY(fov);
        camera->Update(deltaTime);
    }

    // 通常更新へ入る前に入口演出が始まるため、全Objectの描画TransformとLODを
    // 演出カメラ確定後に更新します。deltaTimeは進めず、敵やギミックの時間は停止させます。
    if (objectManager_) {
        objectManager_->Update(0.0f);
    }
    if (particleSystem_) {
        particleSystem_->Update(deltaTime);
    }
    GPUParticleManager::GetInstance()->Update(deltaTime);

    if (stageEntryPresentationTimer_ >= kStageEntryPresentationDuration) {
        FinishStageEntryPresentation();
    }
}

void GamePlayScene::FinishStageEntryPresentation() {
    if (!stageEntryPresentationActive_) {
        return;
    }

    stageEntryPresentationActive_ = false;
    stageEntryPresentationPending_ = false;
    stageEntryPresentationCompleted_ = true;
    stageEntryPlayerEmergenceStarted_ = false;
    stageEntryPresentationTimer_ = 0.0f;
    stageEntryPresentationRetryTimer_ = 0.0f;
    if (auto* gate = dynamic_cast<GimmickStageGate*>(stageEntryGate_)) {
        gate->SetTransitionEnabled(true);
    }
    stageEntryGate_ = nullptr;
    if (stageEntryCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            params->cinemaBarHeight = stageEntryCinemaBarBaseHeight_;
        }
        stageEntryCinemaBarOverrideActive_ = false;
        stageEntryCinemaBarBaseHeight_ = 0.0f;
    }
    if (player_) {
        player_->StopGateReturnAnimation(stageEntryHadPlayerControl_);
        player_->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
        player_->TriggerSlimeImpulse({ 0.96f, 1.08f, 0.96f }, 0.18f);
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetFovY(stageEntryCameraBaseFov_);
#ifdef USE_IMGUI
        if (CameraEditor::GetInstance()->IsEditorMode()) {
            camera->SetInputEnabled(true);
            CameraEditor::GetInstance()->Update(player_, false);
        } else
#endif
        {
            if (player_) {
                camera->SetFollowTarget(player_);
                camera->SetFollowMode(Camera::FollowMode::kAimable);
                camera->SetRotation({
                    kStageEntryThirdPersonPitch,
                    std::atan2(stageEntryDirection_.x, stageEntryDirection_.z)
                        + kStageEntryCameraYawOffset,
                    0.0f
                });
                camera->SnapToThirdPerson(
                    kStageEntryThirdPersonDistance,
                    kStageEntryThirdPersonHeight,
                    kStageEntryThirdPersonPitch);
            }
            camera->SetInputEnabled(true);
        }
    }
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
}

void GamePlayScene::StartArenaBossIntro(
    Object3d* bossObject,
    Object3d* gateObject,
    float bossRevealDelay) {
    if (arenaBossIntroActive_ || isGoal_ || !player_ || !bossObject || !gateObject) {
        return;
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        return;
    }

    arenaBossIntroActive_ = true;
    arenaBossIntroTimer_ = 0.0f;
    arenaBossIntroRevealDelay_ = std::clamp(bossRevealDelay, 0.20f, 1.40f);
    arenaBossIntroBoss_ = bossObject;
    arenaBossIntroGate_ = gateObject;
    arenaBossIntroHadPlayerControl_ = player_->IsControlActive();
    arenaBossIntroGameplayEye_ = camera->GetEye();
    arenaBossIntroGameplayTarget_ = camera->GetTargetPoint();
    arenaBossIntroBaseFov_ = camera->GetFovY();

    const Vector3 playerPosition = player_->GetWorldPosition();
    const Vector3 bossPosition = bossObject->GetWorldPosition();
    Vector3 gatePosition = gateObject->GetWorldPosition();
    if (auto* bossGate = dynamic_cast<GimmickBossGate*>(gateObject)) {
        gatePosition = bossGate->GetClosedPosition();
    }
    const Vector3 bossToPlayer = NormalizePlanarDirection(
        playerPosition - bossPosition,
        { 0.0f, 0.0f, 1.0f });
    const Vector3 right = { bossToPlayer.z, 0.0f, -bossToPlayer.x };

    // 閉じた格子より内側へ収まる戦闘用構図を別に保持し、入口直前の
    // カメラ衝突状態へ戻してしまわないようにします。
    arenaBossIntroRestoreTarget_ = playerPosition
        + Vector3{ 0.0f, kArenaBossGameplayCameraHeight, 0.0f };
    arenaBossIntroRestoreEye_ = arenaBossIntroRestoreTarget_
        + bossToPlayer * kArenaBossGameplayCameraDistance
        + Vector3{ 0.0f, 3.7f, 0.0f };

    // 最初に背後の格子ゲートを見せ、封鎖されたことを一目で理解できる構図にします。
    arenaBossIntroGateTarget_ = gatePosition + Vector3{ 0.0f, 3.85f, 0.0f };
    arenaBossIntroGateEye_ = gatePosition
        - bossToPlayer * 22.0f
        + right * 2.6f
        + Vector3{ 0.0f, 5.35f, 0.0f };

    // 正面全身を画面中央へ収め、登場時の落下、潰れ、跳ね返りを見せます。
    arenaBossIntroBossTarget_ = bossPosition + Vector3{ 0.0f, 4.70f, 0.0f };
    arenaBossIntroBossEye_ = bossPosition
        + bossToPlayer * 31.0f
        + right * 2.6f
        + Vector3{ 0.0f, 9.2f, 0.0f };

    player_->BeginCinematicLock();
    player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetLockOnTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    CameraManager::GetInstance()->SetActiveCamera(camera);

    if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
        arenaBossIntroCinemaBarBaseHeight_ = params->cinemaBarHeight;
        arenaBossIntroCinemaBarOverrideActive_ = true;
    }

    DebugConsole::GetInstance()->AddLog("Stage 3: False King boss introduction started.");
}

void GamePlayScene::UpdateArenaBossIntro(float deltaTime) {
    if (!arenaBossIntroActive_ || !player_ || !arenaBossIntroBoss_ || !arenaBossIntroGate_) {
        FinishArenaBossIntro();
        return;
    }

    arenaBossIntroTimer_ += std::max(deltaTime, 0.0f);
    player_->SetVelocity({ 0.0f, 0.0f, 0.0f });

    if (arenaBossIntroCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            const float openRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(arenaBossIntroTimer_, 0.0f, 0.24f),
                AnimationInterpolation::EasingType::SmootherStep);
            const float closeRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(
                    arenaBossIntroTimer_, kArenaBossIntroBossHoldEnd, kArenaBossIntroDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            const float blend = openRate * (1.0f - closeRate);
            params->cinemaBarHeight = arenaBossIntroCinemaBarBaseHeight_
                + (std::max(arenaBossIntroCinemaBarBaseHeight_, kArenaBossIntroCinemaBarHeight)
                    - arenaBossIntroCinemaBarBaseHeight_) * blend;
        }
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        FinishArenaBossIntro();
        return;
    }

    const Vector3 currentBossPosition = arenaBossIntroBoss_->GetWorldPosition();
    const float authoredBossY = arenaBossIntroBossTarget_.y - 4.70f;
    const float bossRise = std::clamp(currentBossPosition.y - authoredBossY, -0.4f, 3.6f);
    const Vector3 trackedBossTarget = arenaBossIntroBossTarget_
        + Vector3{ 0.0f, bossRise * 0.35f, 0.0f };

    Vector3 eye = arenaBossIntroGameplayEye_;
    Vector3 target = arenaBossIntroGameplayTarget_;
    float fov = arenaBossIntroBaseFov_;
    if (arenaBossIntroTimer_ < kArenaBossIntroGateShotEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(
                arenaBossIntroTimer_, 0.0f, kArenaBossIntroGateShotEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        const Vector3 gateRetreatDirection = NormalizePlanarDirection(
            arenaBossIntroGateEye_ - arenaBossIntroGateTarget_,
            { 0.0f, 0.0f, -1.0f });
        const Vector3 gateDollyStartEye = arenaBossIntroGateEye_ + gateRetreatDirection * 2.8f;
        eye = AnimationInterpolation::Lerp(gateDollyStartEye, arenaBossIntroGateEye_, rate);
        target = arenaBossIntroGateTarget_;
        fov = AnimationInterpolation::Lerp(0.55f, 0.52f, rate);
    } else if (arenaBossIntroTimer_ < kArenaBossIntroGateHoldEnd) {
        eye = arenaBossIntroGateEye_;
        target = arenaBossIntroGateTarget_;
        fov = 0.52f;
    } else if (arenaBossIntroTimer_ < kArenaBossIntroBossFocusEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(
                arenaBossIntroTimer_, kArenaBossIntroGateHoldEnd, kArenaBossIntroBossFocusEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(arenaBossIntroGateEye_, arenaBossIntroBossEye_, rate);
        target = AnimationInterpolation::Lerp(arenaBossIntroGateTarget_, trackedBossTarget, rate);
        fov = AnimationInterpolation::Lerp(0.52f, 0.51f, rate);
    } else if (arenaBossIntroTimer_ < kArenaBossIntroBossHoldEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(
                arenaBossIntroTimer_, kArenaBossIntroBossFocusEnd, kArenaBossIntroBossHoldEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        const Vector3 heroEye = arenaBossIntroBossEye_ + Vector3{ -1.10f * rate, 0.24f * rate, 0.0f };
        eye = heroEye;
        target = trackedBossTarget;
        fov = AnimationInterpolation::Lerp(0.51f, 0.48f, rate);
    } else {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(
                arenaBossIntroTimer_, kArenaBossIntroBossHoldEnd, kArenaBossIntroDuration),
            AnimationInterpolation::EasingType::SmootherStep);
        const Vector3 heroEye = arenaBossIntroBossEye_ + Vector3{ -1.10f, 0.24f, 0.0f };
        eye = AnimationInterpolation::Lerp(heroEye, arenaBossIntroRestoreEye_, rate);
        target = AnimationInterpolation::Lerp(trackedBossTarget, arenaBossIntroRestoreTarget_, rate);
        fov = AnimationInterpolation::Lerp(0.48f, arenaBossIntroBaseFov_, rate);
    }

    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(eye);
    camera->SetTarget(target);
    const Vector3 rotation = MakeLookAtEuler(eye, target);
    camera->SetRotation(rotation);
    camera->ConfigFixedPoint(eye, rotation);
    camera->SetFovY(fov);
    camera->Update(deltaTime);
    CameraManager::GetInstance()->SetActiveCamera(camera);

    if (arenaBossIntroTimer_ >= kArenaBossIntroDuration) {
        FinishArenaBossIntro();
    }
}

void GamePlayScene::FinishArenaBossIntro() {
    if (!arenaBossIntroActive_) {
        return;
    }

    arenaBossIntroActive_ = false;
    arenaBossIntroTimer_ = 0.0f;
    arenaBossIntroBoss_ = nullptr;
    arenaBossIntroGate_ = nullptr;

    if (arenaBossIntroCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            params->cinemaBarHeight = arenaBossIntroCinemaBarBaseHeight_;
        }
        arenaBossIntroCinemaBarOverrideActive_ = false;
        arenaBossIntroCinemaBarBaseHeight_ = 0.0f;
    }

    if (player_ && player_->IsCinematicLocked()) {
        player_->EndCinematicLock(arenaBossIntroHadPlayerControl_);
        player_->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetEye(arenaBossIntroRestoreEye_);
        camera->SetTarget(arenaBossIntroRestoreTarget_);
        camera->SetFovY(arenaBossIntroBaseFov_);
        camera->SetRotation(MakeLookAtEuler(arenaBossIntroRestoreEye_, arenaBossIntroRestoreTarget_));
        camera->SetFollowTarget(player_);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
        camera->SnapToThirdPerson(
            kArenaBossGameplayCameraDistance,
            kArenaBossGameplayCameraHeight,
            kArenaBossGameplayCameraPitch);
        camera->SetInputEnabled(true);
    }
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
}

void GamePlayScene::StartArenaBossDefeatReward(Object3d* rewardObject) {
    if (arenaBossRewardActive_ || isGoal_ || !player_ || !rewardObject) {
        return;
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        return;
    }

    arenaBossRewardActive_ = true;
    arenaBossRewardTimer_ = 0.0f;
    arenaBossRewardObject_ = rewardObject;
    arenaBossRewardHadPlayerControl_ = player_->IsControlActive();
    arenaBossRewardGameplayEye_ = camera->GetEye();
    arenaBossRewardGameplayTarget_ = camera->GetTargetPoint();
    arenaBossRewardBaseFov_ = camera->GetFovY();

    // EventReceiverの起動直後はWorld行列更新前なので、親を持たない報酬王冠の編集値を直接使います。
    const Vector3 crownPosition = rewardObject->GetTranslate();
    const Vector3 playerPosition = player_->GetWorldPosition();
    const Vector3 crownToPlayer = NormalizePlanarDirection(
        playerPosition - crownPosition,
        { 0.0f, 0.0f, 1.0f });
    const Vector3 right = { crownToPlayer.z, 0.0f, -crownToPlayer.x };
    arenaBossRewardFocusTarget_ = crownPosition + Vector3{ 0.0f, 1.15f, 0.0f };
    arenaBossRewardFocusEye_ = crownPosition
        + crownToPlayer * 7.4f
        + right * 2.9f
        + Vector3{ 0.0f, 3.4f, 0.0f };

    player_->BeginCinematicLock();
    player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetLockOnTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    CameraManager::GetInstance()->SetActiveCamera(camera);

    if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
        arenaBossRewardCinemaBarBaseHeight_ = params->cinemaBarHeight;
        arenaBossRewardCinemaBarOverrideActive_ = true;
    }
    DebugConsole::GetInstance()->AddLog("Stage 3: boss crown reward cinematic started.");
}

void GamePlayScene::UpdateArenaBossDefeatReward(float deltaTime) {
    if (!arenaBossRewardActive_ || !player_ || !arenaBossRewardObject_) {
        FinishArenaBossDefeatReward();
        return;
    }

    arenaBossRewardTimer_ += (std::max)(0.0f, deltaTime);
    player_->SetVelocity({ 0.0f, 0.0f, 0.0f });

    if (arenaBossRewardCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            const float openRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(arenaBossRewardTimer_, 0.0f, 0.20f),
                AnimationInterpolation::EasingType::SmootherStep);
            const float closeRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(
                    arenaBossRewardTimer_, kArenaBossRewardTrackEnd, kArenaBossRewardDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            const float blend = openRate * (1.0f - closeRate);
            params->cinemaBarHeight = arenaBossRewardCinemaBarBaseHeight_
                + ((std::max)(arenaBossRewardCinemaBarBaseHeight_, kArenaBossRewardCinemaBarHeight)
                    - arenaBossRewardCinemaBarBaseHeight_) * blend;
        }
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        FinishArenaBossDefeatReward();
        return;
    }

    const Vector3 crownPosition = arenaBossRewardObject_->GetTranslate();
    Vector3 eye = arenaBossRewardGameplayEye_;
    Vector3 target = arenaBossRewardGameplayTarget_;
    float fov = arenaBossRewardBaseFov_;
    if (arenaBossRewardTimer_ < kArenaBossRewardFocusInEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(
                arenaBossRewardTimer_, 0.0f, kArenaBossRewardFocusInEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(arenaBossRewardGameplayEye_, arenaBossRewardFocusEye_, rate);
        target = AnimationInterpolation::Lerp(arenaBossRewardGameplayTarget_, arenaBossRewardFocusTarget_, rate);
        fov = AnimationInterpolation::Lerp(arenaBossRewardBaseFov_, 0.42f, rate);
    } else if (arenaBossRewardTimer_ < kArenaBossRewardTrackEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(
                arenaBossRewardTimer_, kArenaBossRewardFocusInEnd, kArenaBossRewardTrackEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        const Vector3 trackedTarget = crownPosition + Vector3{ 0.0f, 1.0f, 0.0f };
        const Vector3 trackedEye = crownPosition + Vector3{ 6.2f, 3.3f, 7.4f };
        eye = AnimationInterpolation::Lerp(arenaBossRewardFocusEye_, trackedEye, rate);
        target = AnimationInterpolation::Lerp(arenaBossRewardFocusTarget_, trackedTarget, rate);
        fov = AnimationInterpolation::Lerp(0.42f, 0.47f, rate);
    } else {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(
                arenaBossRewardTimer_, kArenaBossRewardTrackEnd, kArenaBossRewardDuration),
            AnimationInterpolation::EasingType::SmootherStep);
        const Vector3 settleTarget = crownPosition + Vector3{ 0.0f, 1.0f, 0.0f };
        const Vector3 settleEye = crownPosition + Vector3{ 6.2f, 3.3f, 7.4f };
        eye = AnimationInterpolation::Lerp(settleEye, arenaBossRewardGameplayEye_, rate);
        target = AnimationInterpolation::Lerp(settleTarget, arenaBossRewardGameplayTarget_, rate);
        fov = AnimationInterpolation::Lerp(0.47f, arenaBossRewardBaseFov_, rate);
    }

    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(eye);
    camera->SetTarget(target);
    const Vector3 rotation = MakeLookAtEuler(eye, target);
    camera->SetRotation(rotation);
    camera->ConfigFixedPoint(eye, rotation);
    camera->SetFovY(fov);
    camera->Update(deltaTime);
    CameraManager::GetInstance()->SetActiveCamera(camera);

    if (arenaBossRewardTimer_ >= kArenaBossRewardDuration) {
        FinishArenaBossDefeatReward();
    }
}

void GamePlayScene::FinishArenaBossDefeatReward() {
    if (!arenaBossRewardActive_) {
        return;
    }

    arenaBossRewardActive_ = false;
    arenaBossRewardTimer_ = 0.0f;
    arenaBossRewardObject_ = nullptr;

    if (arenaBossRewardCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            params->cinemaBarHeight = arenaBossRewardCinemaBarBaseHeight_;
        }
        arenaBossRewardCinemaBarOverrideActive_ = false;
        arenaBossRewardCinemaBarBaseHeight_ = 0.0f;
    }

    if (player_ && player_->IsCinematicLocked()) {
        player_->EndCinematicLock(arenaBossRewardHadPlayerControl_);
        player_->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetEye(arenaBossRewardGameplayEye_);
        camera->SetTarget(arenaBossRewardGameplayTarget_);
        camera->SetFovY(arenaBossRewardBaseFov_);
        camera->SetRotation(MakeLookAtEuler(arenaBossRewardGameplayEye_, arenaBossRewardGameplayTarget_));
        camera->SetFollowTarget(player_);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
        camera->SetInputEnabled(true);
    }
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
}

void GamePlayScene::UpdateGoalCrownIdleAnimation(float deltaTime) {
    if (!objectManager_ || isGoal_) {
        return;
    }

    constexpr float kRotationSpeed = 0.62f;
    constexpr float kBobAngularSpeed = 1.75f;
    constexpr float kBobAmplitude = 0.14f;
    constexpr float kFarSparkleInterval = 0.34f;
    constexpr float kNearSparkleInterval = 0.18f;
    constexpr float kNearDistance = 6.0f;
    constexpr float kFarDistance = 18.0f;
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
    float strongestNearRate = 0.0f;

    for (auto& object : objectManager_->GetObjects()) {
        if (!object || object->GetEventType() != EventType::Goal) {
            continue;
        }
        foundGoalCrown = true;

        float nearRate = 0.0f;
        if (player_) {
            const Vector3 toCrown = object->GetWorldPosition() - player_->GetWorldPosition();
            const float distance = std::sqrt(
                toCrown.x * toCrown.x +
                toCrown.y * toCrown.y +
                toCrown.z * toCrown.z);
            nearRate = 1.0f - std::clamp(
                (distance - kNearDistance) / (kFarDistance - kNearDistance),
                0.0f,
                1.0f);
            strongestNearRate = std::max(strongestNearRate, nearRate);
        }

        Vector3 position = object->GetTranslate();
        position.y += bobDelta;
        object->SetTranslate(position);

        Vector3 rotation = object->GetRotation();
        rotation.y += kRotationSpeed * deltaTime;
        object->SetRotation(rotation);
        object->SetEmissive(isCleared
            ? 1.20f + emissivePulse * 0.38f + nearRate * 0.14f
            : 1.62f + emissivePulse * 0.62f + nearRate * 0.48f);
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();

        const Matrix4x4& crownWorld = object->GetWorldMatrix();
        if (emitSparkle) {
            const Vector3& localOffset = kSparkleOffsets[goalCrownSparklePatternIndex_ % kSparkleOffsets.size()];
            GPUParticleManager::GetInstance()->Emit(kGoalCrownSparklePreset, Math::Transform(localOffset, crownWorld));

            const bool emitHeroTwinkle =
                goalCrownSparklePatternIndex_ % 4 == 0 ||
                (nearRate >= 0.55f && goalCrownSparklePatternIndex_ % 2 == 0);
            if (emitHeroTwinkle) {
                const Vector3& twinkleOffset = kSparkleOffsets[
                    (goalCrownSparklePatternIndex_ + 3) % kSparkleOffsets.size()];
                GPUParticleManager::GetInstance()->Emit(
                    kGoalCrownTwinklePreset,
                    Math::Transform(twinkleOffset, crownWorld));
            }
        }
    }

    if (!foundGoalCrown) {
        return;
    }
    if (emitSparkle) {
        goalCrownSparkleTimer_ += kFarSparkleInterval +
            (kNearSparkleInterval - kFarSparkleInterval) * strongestNearRate;
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
            SceneManager::GetInstance()->ChangeSceneAfterFade("SELECT");
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
        if (t >= animation.jumpStartTime && t < animation.victoryLandTime) {
            const float jumpRate = AnimationInterpolation::SegmentRate(t, animation.jumpStartTime, animation.victoryLandTime);
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
            goalWasStageCleared_ ? "crown_get_silver_cue" : "crown_get_cue",
            goalCrownPosition_,
            { crownScale, crownScale, crownScale });
        goalLandingCuePlayed_ = true;
    }

    if (!goalResultCuePlayed_ && t >= animation.apexTime) {
        Vector3 resultPosition = goalPlayerPosePosition_;
        resultPosition.y += std::max(1.25f, goalClearPlayerAnimator_.GetCurrentScale().y * 0.50f);
        VFXSequencer::PlayOneShot(
            goalWasStageCleared_ ? "crown_result_silver_cue" : "crown_result_cue",
            resultPosition);
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
        + goalMoveForward_ * animation.forwardDistance;
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
        // 王冠の弧に小さなパララックスを与え、着地前から次の構図へ受け渡します。
        const float moveRate = AnimationInterpolation::SegmentRate(
            t,
            camera.crownMoveStartTime,
            animation.crownLandTime);
        const float orbit = std::sin(moveRate * kPi);
        movingCrownEye = movingCrownEye
            + goalMoveRight_ * (orbit * 0.35f)
            + Vector3{ 0.0f, orbit * 0.12f, 0.0f };
        const float handoffRaw = std::clamp((moveRate - 0.52f) / 0.48f, 0.0f, 1.0f);
        const float handoff = AnimationInterpolation::ApplyEasing(
            handoffRaw,
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(movingCrownEye, goalCameraLandingEye_, handoff);
        target = AnimationInterpolation::Lerp(movingCrownTarget, goalCameraLandingTarget_, handoff);
        fov = AnimationInterpolation::Lerp(camera.crownFocusFov, camera.landingFov, handoff);
    } else if (t < animation.jumpStartTime) {
        const float p = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(t, animation.crownLandTime, animation.jumpStartTime),
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(goalCameraLandingEye_, goalCameraJumpEye_, p);
        target = AnimationInterpolation::Lerp(goalCameraLandingTarget_, goalCameraJumpTarget_, p);
        fov = AnimationInterpolation::Lerp(camera.landingFov, camera.jumpFov, p);
    } else if (t < animation.victoryLandTime) {
        Vector3 jumpTarget = goalPlayerPosePosition_ + Vector3{ 0.0f, std::max(0.82f, goalClearPlayerAnimator_.GetCurrentScale().y * 0.46f), 0.0f };
        const float airborneRate = AnimationInterpolation::SegmentRate(
            t,
            animation.jumpStartTime,
            animation.victoryLandTime);
        const float cameraArc = std::sin(airborneRate * kPi);
        Vector3 jumpEye = jumpTarget
            + goalMoveForward_ * camera.jumpCameraDistance
            + goalMoveRight_ * (camera.jumpCameraSide + cameraArc * 0.55f)
            + Vector3{ 0.0f, camera.jumpCameraHeight + cameraArc * 0.30f, 0.0f };

        float settle = 0.0f;
        if (t >= animation.apexTime) {
            const float fallRate = AnimationInterpolation::SegmentRate(
                t,
                animation.apexTime,
                animation.victoryLandTime);
            const float settleRaw = std::clamp((fallRate - 0.28f) / 0.72f, 0.0f, 1.0f);
            settle = AnimationInterpolation::ApplyEasing(
                settleRaw,
                AnimationInterpolation::EasingType::SmootherStep);
        }
        eye = AnimationInterpolation::Lerp(jumpEye, goalCameraResultEye_, settle);
        target = AnimationInterpolation::Lerp(jumpTarget, goalCameraResultTarget_, settle);
        fov = AnimationInterpolation::Lerp(camera.jumpFov, camera.resultFov, settle);
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

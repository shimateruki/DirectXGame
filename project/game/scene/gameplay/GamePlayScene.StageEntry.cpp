#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "GPUParticleManager.h"
#include "PostEffect.h"
#include "game/actor/gimmick/stage/GimmickStageGate.h"
#include "engine/utility/math/AnimationInterpolation.h"
#include <algorithm>
#include <cmath>
#include <numbers>

#include "game/scene/gameplay/GamePlayScene.PresentationMath.h"
using namespace GamePlayPresentationMath;

namespace {
constexpr float kStageEntryCameraIntroDuration = 0.24f;
constexpr float kStageEntryGateOpenLeadTime = 0.42f;
constexpr float kStageEntryCameraRestoreStartTime = 1.62f;
constexpr float kStageEntryCameraRestoreDuration = 1.05f;
constexpr float kStageEntryPresentationDuration = kStageEntryCameraRestoreStartTime + kStageEntryCameraRestoreDuration;
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
} // namespace

// ステージ入場演出の開始・更新・復帰。PlayerとCameraは借用し、終了時の操作復帰とゲート再有効化を維持する。
// 入口演出中はプレイヤー操作とカメラを一時的に預かり、終了時に元の状態へ戻します。

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

// 開始時に通常のスポーン地点を保存し、入口ゲートから出現する演出位置へ一時的に移します。
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
    stageEntryDirection_ = NormalizePlanarDirection(playerSpawn - gatePosition, {1.0f, 0.0f, 0.0f});

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

    const Vector3 gateFocus = {gatePosition.x, playerSpawn.y + kStageEntryCameraFocusHeight, gatePosition.z};
    const Vector3 landingFocus = playerSpawn + Vector3{0.0f, kStageEntryCameraFocusHeight, 0.0f};
    stageEntryCameraFocusTarget_ =
        AnimationInterpolation::Lerp(gateFocus, landingFocus, kStageEntryCameraLandingFocusRate);
    // ステージ内部の中庭へ直進すると地形内へ入るため、入口の外側から斜めに見ます。
    const float thirdPersonYaw =
        std::atan2(stageEntryDirection_.x, stageEntryDirection_.z) + kStageEntryCameraYawOffset;
    const Matrix4x4 cinematicYawRotation = Math::MakeRotateYMatrix(thirdPersonYaw);
    stageEntryCameraFocusEye_ =
        stageEntryCameraFocusTarget_ +
        Math::TransformNormal({0.0f, 0.0f, -kStageEntryCameraBackDistance}, cinematicYawRotation) +
        Vector3{0.0f, kStageEntryCameraHeight, 0.0f};
    // 編集中の自由カメラ位置を引き継がず、毎回ゲートを読める決定的な導入ショットから始めます。
    const Vector3 cinematicBackDirection =
        NormalizePlanarDirection(stageEntryCameraFocusEye_ - stageEntryCameraFocusTarget_,
                                 {-stageEntryDirection_.x, 0.0f, -stageEntryDirection_.z});
    stageEntryCameraStartTarget_ = gateFocus;
    stageEntryCameraStartEye_ = stageEntryCameraFocusEye_ + cinematicBackDirection * 2.0f + Vector3{0.0f, 0.6f, 0.0f};
    // 復帰先はSnapToThirdPersonと同じ式で作り、演出終了フレームの段差をなくします。
    const Matrix4x4 thirdPersonRotation =
        Math::MakeRotateXMatrix(kStageEntryThirdPersonPitch) * Math::MakeRotateYMatrix(thirdPersonYaw);
    stageEntryCameraRestoreTarget_ = playerSpawn + Vector3{0.0f, kStageEntryThirdPersonHeight, 0.0f};
    stageEntryCameraRestoreEye_ =
        stageEntryCameraRestoreTarget_ +
        Math::TransformNormal({0.0f, 0.0f, -kStageEntryThirdPersonDistance}, thirdPersonRotation);

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
        if (CameraEditor::GetInstance()->ResolveSceneCameraPose(authoredEntryCamera, authoredEye, authoredTarget)) {
            stageEntryCameraStartEye_ = authoredEye;
            stageEntryCameraStartTarget_ = authoredTarget;
            stageEntryCameraFocusEye_ = authoredEye;
            stageEntryCameraFocusTarget_ = authoredTarget;
            stageEntryCameraFocusFov_ = authoredEntryCamera->GetSceneCameraSettings().fovY;
            DebugConsole::GetInstance()->AddLog("Stage Entry: using authored camera for Stage1_EntranceGate.");
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
    const Vector3 startRotation = MakeLookAtEuler(stageEntryCameraStartEye_, stageEntryCameraStartTarget_);
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
    if (!stageEntryPlayerEmergenceStarted_ && stageEntryPresentationTimer_ >= kStageEntryGateOpenLeadTime) {
        stageEntryPlayerEmergenceStarted_ = true;
        player_->SetIsVisible(true);
    }
    if (stageEntryPlayerEmergenceStarted_) {
        player_->Update(deltaTime);
    } else {
        player_->SetIsVisible(false);
        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }
    if (stageEntryGate_) {
        stageEntryGate_->Update(deltaTime);
    }

    if (stageEntryCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            const float openRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(stageEntryPresentationTimer_, 0.0f,
                                                    kStageEntryCinemaBarOpenDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            const float closeRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(stageEntryPresentationTimer_, kStageEntryCameraRestoreStartTime,
                                                    kStageEntryPresentationDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            const float blend = openRate * (1.0f - closeRate);
            const float targetHeight = std::max(stageEntryCinemaBarBaseHeight_, kStageEntryCinemaBarHeight);
            params->cinemaBarHeight =
                stageEntryCinemaBarBaseHeight_ + (targetHeight - stageEntryCinemaBarBaseHeight_) * blend;
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
                AnimationInterpolation::SegmentRate(stageEntryPresentationTimer_, 0.0f, kStageEntryCameraIntroDuration),
                AnimationInterpolation::EasingType::EaseOut);
            eye = AnimationInterpolation::Lerp(stageEntryCameraStartEye_, stageEntryCameraFocusEye_, introRate);
            target =
                AnimationInterpolation::Lerp(stageEntryCameraStartTarget_, stageEntryCameraFocusTarget_, introRate);
        } else {
            const float restoreRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(stageEntryPresentationTimer_, kStageEntryCameraRestoreStartTime,
                                                    kStageEntryPresentationDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            eye = AnimationInterpolation::Lerp(stageEntryCameraFocusEye_, stageEntryCameraRestoreEye_, restoreRate);
            target =
                AnimationInterpolation::Lerp(stageEntryCameraFocusTarget_, stageEntryCameraRestoreTarget_, restoreRate);
            fov = AnimationInterpolation::Lerp(stageEntryCameraFocusFov_, stageEntryCameraBaseFov_, restoreRate);
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

// 演出の一時設定を解除し、ゲート・プレイヤー・カメラの通常制御を再開します。
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
        player_->TriggerSlimeImpulse({0.96f, 1.08f, 0.96f}, 0.18f);
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
                camera->SetRotation(
                    {kStageEntryThirdPersonPitch,
                     std::atan2(stageEntryDirection_.x, stageEntryDirection_.z) + kStageEntryCameraYawOffset, 0.0f});
                camera->SnapToThirdPerson(kStageEntryThirdPersonDistance, kStageEntryThirdPersonHeight,
                                          kStageEntryThirdPersonPitch);
            }
            camera->SetInputEnabled(true);
        }
    }
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
}

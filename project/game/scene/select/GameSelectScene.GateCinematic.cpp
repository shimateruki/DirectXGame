#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include "Camera.h"
#include "Player.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "game/system/progress/StageManager.h"
#include "SceneManager.h"
#include "PostEffect.h"
#include "GimmickStageGate.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include "game/scene/select/GameSelectScene.Internal.h"
using namespace StageSelectDetail;

void GameSelectScene::UpdateGateCinematicBars(float deltaTime) {
    // シネマバーの高さを既定値から補間し、他のポストエフェクト設定を保つ。
    PostEffect::Params* params = PostEffect::GetInstance()->GetParams();

    if (!params) {
        return;
    }

    const bool isCinematicActive =
        gateEntryCinematicActive_ || gateReturnPresentationActive_ || unlockingStageIndex_ >= 0;
    if (isCinematicActive && !gateCinematicBarOverrideActive_) {
        gateCinematicBarBaseHeight_ = params->cinemaBarHeight;
        gateCinematicBarOverrideActive_ = true;
    }

    if (!gateCinematicBarOverrideActive_) {
        return;
    }

    const float targetBlend = isCinematicActive ? 1.0f : 0.0f;
    const float speed = isCinematicActive ? kGateCinematicBarOpenSpeed : kGateCinematicBarCloseSpeed;
    const float step = speed * std::max(deltaTime, 0.0f);

    if (gateCinematicBarBlend_ < targetBlend) {
        gateCinematicBarBlend_ = std::min(targetBlend, gateCinematicBarBlend_ + step);
    } else if (gateCinematicBarBlend_ > targetBlend) {
        gateCinematicBarBlend_ = std::max(targetBlend, gateCinematicBarBlend_ - step);
    }

    const float targetHeight = std::max(gateCinematicBarBaseHeight_, kGateCinematicBarHeight);
    params->cinemaBarHeight =
        gateCinematicBarBaseHeight_ + (targetHeight - gateCinematicBarBaseHeight_) * gateCinematicBarBlend_;

    if (!isCinematicActive && gateCinematicBarBlend_ <= 0.0f) {
        params->cinemaBarHeight = gateCinematicBarBaseHeight_;
        gateCinematicBarBaseHeight_ = 0.0f;
        gateCinematicBarOverrideActive_ = false;
    }
}

void GameSelectScene::ResetGateCinematicBars() {
    // この演出が上書きした場合だけ元のバー高さへ復元する。
    PostEffect::Params* params = PostEffect::GetInstance()->GetParams();

    if (params && gateCinematicBarOverrideActive_) {
        params->cinemaBarHeight = gateCinematicBarBaseHeight_;
    }

    gateCinematicBarOverrideActive_ = false;
    gateCinematicBarBlend_ = 0.0f;
    gateCinematicBarBaseHeight_ = 0.0f;
}

bool GameSelectScene::StartStageReturnPresentation(const GameDataManager::StageSelectReturnPresentation& request) {
    if (!request.active || gateEntryCinematicActive_) {
        return false;
    }

    const auto& stages = StageManager::GetInstance()->GetStages();

    if (request.stageIndex < 0 || request.stageIndex >= static_cast<int>(stages.size())) {
        return false;
    }

    gateReturnTargetGate_ = FindStageGateByIndex(request.stageIndex);

    if (!gateReturnTargetGate_ || !player_) {
        return false;
    }

    const engine::Vector3 gatePos = gateReturnTargetGate_->GetWorldPosition();
    const engine::Vector3 selectPlayerStart = player_->GetWorldPosition();
    const engine::Vector3 stageNodePosition = GetStageNodePosition(request.stageIndex);
    const engine::Vector3 fallbackToGate = {gatePos.x - stageNodePosition.x, 0.0f, gatePos.z - stageNodePosition.z};
    const float fallbackLengthSq = fallbackToGate.x * fallbackToGate.x + fallbackToGate.z * fallbackToGate.z;
    const engine::Vector3 fallbackFromPlayer = {gatePos.x - selectPlayerStart.x, 0.0f, gatePos.z - selectPlayerStart.z};
    const engine::Vector3 gateDirection = SelectGateEntryDirection(
        gateReturnTargetGate_, fallbackLengthSq > 0.0001f ? fallbackToGate : fallbackFromPlayer);
    const engine::Vector3 outwardDirection =
        SelectNormalizeGateDirectionXZ({-gateDirection.x, 0.0f, -gateDirection.z}, {0.0f, 0.0f, -1.0f});
    const SelectGateEntryRoute probeRoute = SelectBuildGateEntryRoute(gatePos, gateDirection, selectPlayerStart.y);
    const engine::Vector3 landingProbePosition = {probeRoute.surface.x + outwardDirection.x * kGateReturnExitForwardOffset,
                                          selectPlayerStart.y,
                                          probeRoute.surface.z + outwardDirection.z * kGateReturnExitForwardOffset};
    const float returnGroundY = SelectResolveGateReturnGroundY(landingProbePosition, gatePos, stageNodePosition,
                                                               selectPlayerStart, gateReturnTargetGate_);

    // 出現開始はゲートの見た目中心に合わせ、着地先だけを床レイキャストのYに分離します。
    const float gateExitY = gatePos.y;
    const SelectGateEntryRoute route = SelectBuildGateEntryRoute(gatePos, gateDirection, gateExitY);

    gateReturnStageIndex_ = request.stageIndex;
    selectedStageIndex_ = request.stageIndex;
    previousSelectedStageIndex_ = -1;
    unlockingStageIndex_ = -1;
    gateReturnDirection_ = gateDirection;
    gateReturnInsidePlayerPosition_ = route.inside;
    gateReturnCenterPlayerPosition_ = route.center;
    gateReturnSurfacePlayerPosition_ = route.surface;
    gateReturnEndPlayerPosition_ = {route.surface.x + outwardDirection.x * kGateReturnExitForwardOffset, returnGroundY,
                                    route.surface.z + outwardDirection.z * kGateReturnExitForwardOffset};
    gateReturnPlayerScale_ = player_->GetScale();
    gateReturnHadPlayerControl_ = player_->IsControlActive();

    PlayerGateReturnAnimation::Route returnRoute;
    returnRoute.start = gateReturnInsidePlayerPosition_;
    returnRoute.gateCenter = gateReturnCenterPlayerPosition_;
    returnRoute.end = gateReturnEndPlayerPosition_;
    returnRoute.direction = outwardDirection;
    returnRoute.baseScale = gateReturnPlayerScale_;
    player_->StartGateReturnAnimation(returnRoute);

    if (auto* stageGate = dynamic_cast<GimmickStageGate*>(gateReturnTargetGate_)) {
        stageGate->TriggerEntryReaction();
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        const engine::Vector3 side = {gateDirection.z, 0.0f, -gateDirection.x};
        const engine::Vector3 gateFocus = {gatePos.x, returnGroundY + kGateReturnCameraFocusHeight, gatePos.z};
        const engine::Vector3 landingFocus = {gateReturnEndPlayerPosition_.x,
                                      gateReturnEndPlayerPosition_.y + kGateReturnCameraFocusHeight,
                                      gateReturnEndPlayerPosition_.z};

        // ゲート真正面だけを見ると、着地後のプレイヤーが画面外やゲート裏へ逃げやすいです。
        // 注視点を着地点寄りにし、少し斜めから見ることでゲートから出る動きと着地を同時に見せます。
        const engine::Vector3 focus = SelectLerpVector3(gateFocus, landingFocus, kGateReturnCameraLandingFocusRate);
        const engine::Vector3 restoreTarget = {gateReturnEndPlayerPosition_.x,
                                       gateReturnEndPlayerPosition_.y + kGateReturnThirdPersonTargetHeight,
                                       gateReturnEndPlayerPosition_.z};
        const engine::Vector3 restoreEye = {restoreTarget.x - outwardDirection.x * kGateReturnThirdPersonDistance,
                                    restoreTarget.y + kGateReturnThirdPersonHeight,
                                    restoreTarget.z - outwardDirection.z * kGateReturnThirdPersonDistance};
        gateReturnCameraStartEye_ = camera->GetEye();
        gateReturnCameraStartTarget_ = camera->GetTargetPoint();
        gateReturnCameraRestoreEye_ = restoreEye;
        gateReturnCameraRestoreTarget_ = restoreTarget;
        gateReturnCameraEndTarget_ = focus;
        gateReturnCameraEndEye_ = {
            focus.x - gateDirection.x * kGateReturnCameraBackDistance + side.x * kGateReturnCameraSideOffset,
            focus.y + kGateReturnCameraHeight,
            focus.z - gateDirection.z * kGateReturnCameraBackDistance + side.z * kGateReturnCameraSideOffset};
        camera->SetInputEnabled(false);
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::FixedPoint);
        camera->ConfigFixedPoint(gateReturnCameraStartEye_,
                                 SelectLookAtRotation(gateReturnCameraStartEye_, gateReturnCameraStartTarget_));
    }

    gateReturnPresentationActive_ = true;
    gateReturnPresentationTimer_ = 0.0f;
    gateReturnLandingImpulsePlayed_ = false;
    gateReturnRecoveryImpulsePlayed_ = false;
    stageDecisionCooldown_ = kGateReturnTotalDuration;

    if (gatePromptSprite_) {
        gatePromptSprite_->SetVisible(false);
        gatePromptSprite_->Update();
    }

    DebugConsole::GetInstance()->AddLog("Stage Select: gate return presentation start.");
    return true;
}

void GameSelectScene::UpdateStageReturnPresentation(float deltaTime) {
    if (!gateReturnPresentationActive_) {
        return;
    }

    gateReturnPresentationTimer_ += deltaTime;
    const engine::Vector3 outwardDirection =
        SelectNormalizeGateDirectionXZ({-gateReturnDirection_.x, 0.0f, -gateReturnDirection_.z}, {0.0f, 0.0f, -1.0f});

    if (gateReturnTargetGate_) {
        const engine::Vector3 gatePos = gateReturnTargetGate_->GetWorldPosition();
        const engine::Vector3 stageNodePosition =
            gateReturnStageIndex_ >= 0 ? GetStageNodePosition(gateReturnStageIndex_) : gateReturnEndPlayerPosition_;
        const float returnGroundY =
            SelectResolveGateReturnGroundY(gateReturnEndPlayerPosition_, gatePos, stageNodePosition,
                                           gateReturnEndPlayerPosition_, gateReturnTargetGate_);

        // 出現開始はゲートの見た目中心、着地先は床レイキャストで分けます。
        const float gateExitY = gatePos.y;
        const SelectGateEntryRoute route = SelectBuildGateEntryRoute(gatePos, gateReturnDirection_, gateExitY);
        gateReturnInsidePlayerPosition_ = route.inside;
        gateReturnCenterPlayerPosition_ = route.center;
        gateReturnSurfacePlayerPosition_ = route.surface;
        gateReturnEndPlayerPosition_ = {route.surface.x + outwardDirection.x * kGateReturnExitForwardOffset,
                                        returnGroundY,
                                        route.surface.z + outwardDirection.z * kGateReturnExitForwardOffset};
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetInputEnabled(false);
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::FixedPoint);

        engine::Vector3 eye = gateReturnCameraEndEye_;
        engine::Vector3 target = gateReturnCameraEndTarget_;

        if (gateReturnPresentationTimer_ < kGateReturnCameraRestoreStartTime) {
            const float introT =
                SelectEaseOutCubic(SelectClamp01(gateReturnPresentationTimer_ / kGateReturnCameraIntroDuration));
            eye = SelectLerpVector3(gateReturnCameraStartEye_, gateReturnCameraEndEye_, introT);
            target = SelectLerpVector3(gateReturnCameraStartTarget_, gateReturnCameraEndTarget_, introT);
        } else {
            const float restoreT = SelectEaseInOutCubic(SelectClamp01(
                (gateReturnPresentationTimer_ - kGateReturnCameraRestoreStartTime) / kGateReturnCameraRestoreDuration));
            eye = SelectLerpVector3(gateReturnCameraEndEye_, gateReturnCameraRestoreEye_, restoreT);
            target = SelectLerpVector3(gateReturnCameraEndTarget_, gateReturnCameraRestoreTarget_, restoreT);
        }

        camera->ConfigFixedPoint(eye, SelectLookAtRotation(eye, target));
    }

    if (player_) {
        player_->SetIsVisible(true);
        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    const bool playerAnimationFinished = player_ && player_->IsGateReturnAnimationFinished();

    if (playerAnimationFinished || gateReturnPresentationTimer_ >= kGateReturnTotalDuration) {
        FinishStageReturnPresentation();
    }
}

void GameSelectScene::FinishStageReturnPresentation() {
    // 帰還先のゲート参照を外し、直後の誤操作を短いクールダウンで防ぐ。
    gateReturnPresentationActive_ = false;
    gateReturnPresentationTimer_ = 0.0f;
    gateReturnStageIndex_ = -1;
    gateReturnTargetGate_ = nullptr;
    stageDecisionCooldown_ = 0.25f;

    if (player_) {
        player_->StopGateReturnAnimation(gateReturnHadPlayerControl_);
        player_->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
        player_->TriggerSlimeImpulse({0.96f, 1.08f, 0.96f}, 0.18f);
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        if (player_) {
            camera->SetFollowTarget(player_);
            camera->SetFollowMode(Camera::FollowMode::Aimable);
            camera->SnapToThirdPerson(kGateReturnThirdPersonDistance, kGateReturnThirdPersonHeight,
                                      kGateReturnThirdPersonPitch);
        }

        camera->SetInputEnabled(true);
    }

    if (pendingStageClearRewardPresentation_.active) {
        StartStageClearRewardPresentation(pendingStageClearRewardPresentation_);
        pendingStageClearRewardPresentation_ = {};
    } else {
        unlockingStageIndex_ = FindPendingUnlockStage();

        if (unlockingStageIndex_ >= 0) {
            selectedStageIndex_ = unlockingStageIndex_;
            DebugConsole::GetInstance()->AddLog("Stage Select: crown unlock presentation start.");
        }
    }

    DebugConsole::GetInstance()->AddLog("Stage Select: gate return presentation finished.");
}

// 通常の遷移ゲートと同じ移動・ディゾルブを再生し、選択シーンは行き先だけを担当します。
void GameSelectScene::StartGateEntryCinematic(int stageIndex) {
    if (gateEntryCinematicActive_ || !SceneManager::GetInstance()->IsPlaying()) return;
    auto* gate = FindStageGateByIndex(stageIndex);
    if (!gate || !gateEntryPresentation_.Start(player_, gate, false)) return;
    gateEntryTargetGate_ = gate;
    gateEntryCinematicActive_ = true;
    isChangingStage_ = true;
    stageDecisionCooldown_ = 0.35f;
    if (auto* stageGate = dynamic_cast<GimmickStageGate*>(gate)) stageGate->TriggerEntryReaction();
    if (gatePromptSprite_) {
        gatePromptSprite_->SetVisible(false);
        gatePromptSprite_->Update();
    }
    DebugConsole::GetInstance()->AddLog("Stage Select: gate entry cinematic start.");
}

void GameSelectScene::UpdateGateEntryCinematic(float deltaTime) {
    if (!gateEntryCinematicActive_) return;
    gateEntryPresentation_.Update(deltaTime);
    if (!gateEntryPresentation_.IsFinished()) return;
    gateEntryCinematicActive_ = false;
    gateEntryTargetGate_ = nullptr;
    ResetGateCinematicBars();
    SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
}

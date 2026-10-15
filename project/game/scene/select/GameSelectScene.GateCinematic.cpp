#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include "Camera.h"
#include "Player.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "StageManager.h"
#include "SceneManager.h"
#include "PostEffect.h"
#include "GimmickStageGate.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include "game/scene/select/GameSelectScene.Internal.h"
using namespace StageSelectDetail;

// ゲート出入りの演出。材質退避・復元とカメラ復帰をまとめ、遷移条件や実行順は変更しない。
void GameSelectScene::CaptureGateEntryMaterialState(engine::Object3d* rootObject) {
    // ゲート突入演出で変更する前に、子を含む材質を復元用に控える。
    gateEntryMaterialSnapshots_.clear();
    if (!rootObject) {
        return;
    }

    std::vector<engine::Object3d*> stack;
    stack.push_back(rootObject);
    while (!stack.empty()) {
        engine::Object3d* object = stack.back();
        stack.pop_back();
        if (!object) {
            continue;
        }

        GateEntryMaterialSnapshot snapshot;
        snapshot.object = object;
        snapshot.color = object->GetColor();
        snapshot.materialType = object->GetMaterialType();
        snapshot.emissive = object->GetEmissive();
        if (auto* material = object->GetMaterialData()) {
            snapshot.portalClipEnabled = material->portalClipEnabled;
            snapshot.portalClipProgress = material->portalClipProgress;
            snapshot.portalClipCenter = material->portalClipCenter;
            snapshot.portalClipEdgeWidth = material->portalClipEdgeWidth;
            snapshot.portalClipNormal = material->portalClipNormal;
            snapshot.portalClipDissolve = material->portalClipDissolve;
            snapshot.portalClipColor = material->portalClipColor;
        }
        gateEntryMaterialSnapshots_.push_back(snapshot);

        for (engine::Object3d* child : object->GetChildren()) {
            if (child) {
                stack.push_back(child);
            }
        }
    }
}

void GameSelectScene::ApplyGateEntryDissolveMaterial(float progress, const engine::Vector3& gatePosition,
                                                     const engine::Vector3& direction) {
    const float dissolveProgress = SelectClamp01(progress);
    engine::Vector3 dissolveDirection = direction;
    const float directionLength =
        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (directionLength > 0.0001f) {
        dissolveDirection.x /= directionLength;
        dissolveDirection.y /= directionLength;
        dissolveDirection.z /= directionLength;
    } else {
        dissolveDirection = {0.0f, 0.0f, 1.0f};
    }
    // ゲートに入り始めた瞬間から輪郭を焼き、透明化は後半だけに寄せて削れて消える印象を強める。
    const float clipProgress = SelectClamp01(0.14f + SelectEaseOutCubic(dissolveProgress) * 0.86f);
    const float edgePeak = 1.0f - std::abs(clipProgress * 2.0f - 1.0f);
    const float fadeProgress = SelectEaseInOutCubic(SelectClamp01((clipProgress - 0.72f) / 0.28f));
    const float heat = SelectClamp01(0.08f + clipProgress * 0.18f + edgePeak * 0.30f);
    const float edgeWidth = 0.30f - 0.16f * clipProgress;
    const float emissive = 1.25f + clipProgress * 0.95f + edgePeak * 1.45f;
    const float dissolveNoise = 0.74f - 0.18f * clipProgress;
    const engine::Vector4 portalColor = {1.0f, 0.66f, 0.26f, 0.94f};

    for (const GateEntryMaterialSnapshot& snapshot : gateEntryMaterialSnapshots_) {
        engine::Object3d* object = snapshot.object;
        if (!object) {
            continue;
        }

        const engine::Vector4 baseColor = snapshot.color;
        const engine::Vector4 dissolveColor = {baseColor.x * (1.0f - heat) + 1.0f * heat,
                                       baseColor.y * (1.0f - heat) + 0.76f * heat,
                                       baseColor.z * (1.0f - heat) + 0.34f * heat, 1.0f - fadeProgress};

        object->SetMaterialType(4);
        object->SetBlendMode(BlendMode::kNormal);
        object->SetEmissive(std::max(snapshot.emissive, emissive));
        object->SetColor(dissolveColor);

        if (auto* material = object->GetMaterialData()) {
            material->portalClipEnabled = 1.0f;
            material->portalClipProgress = clipProgress;
            material->portalClipCenter = gatePosition;
            material->portalClipNormal = dissolveDirection;
            material->portalClipEdgeWidth = edgeWidth;
            material->portalClipDissolve = dissolveNoise;
            material->portalClipColor = portalColor;
        }
    }
}

void GameSelectScene::RestoreGateEntryMaterialState() {
    // 編集・再入場時にゲートの演出色が残らないよう、控えた材質を戻す。
    for (const GateEntryMaterialSnapshot& snapshot : gateEntryMaterialSnapshots_) {
        engine::Object3d* object = snapshot.object;
        if (!object) {
            continue;
        }

        object->SetMaterialType(snapshot.materialType);
        object->SetEmissive(snapshot.emissive);
        object->SetColor(snapshot.color);

        if (auto* material = object->GetMaterialData()) {
            material->portalClipEnabled = snapshot.portalClipEnabled;
            material->portalClipProgress = snapshot.portalClipProgress;
            material->portalClipCenter = snapshot.portalClipCenter;
            material->portalClipEdgeWidth = snapshot.portalClipEdgeWidth;
            material->portalClipNormal = snapshot.portalClipNormal;
            material->portalClipDissolve = snapshot.portalClipDissolve;
            material->portalClipColor = snapshot.portalClipColor;
        }
    }
    gateEntryMaterialSnapshots_.clear();
}

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
        camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
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
        camera->SetFollowMode(Camera::FollowMode::kFixedPoint);

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
            camera->SetFollowMode(Camera::FollowMode::kAimable);
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

// 接触位置に依存しないゲート基準の進入経路を作り、操作・カメラ・素材の演出状態を確保します。
void GameSelectScene::StartGateEntryCinematic(int stageIndex) {
    if (gateEntryCinematicActive_) {
        return;
    }

    gateEntryTargetGate_ = FindNearestStageGate(nullptr);
    gateEntryStartPlayerPosition_ = player_ ? player_->GetWorldPosition() : engine::Vector3{};
    gateEntryStartPlayerScale_ = player_ ? player_->GetScale() : engine::Vector3{1.0f, 1.0f, 1.0f};
    gateEntryTargetPlayerPosition_ = gateEntryStartPlayerPosition_;
    gateEntrySurfacePlayerPosition_ = gateEntryStartPlayerPosition_;
    gateEntryInsidePlayerPosition_ = gateEntryStartPlayerPosition_;
    gateEntryDirection_ = {0.0f, 0.0f, 1.0f};
    gateEntryCameraStartEye_ = {};
    gateEntryCameraStartTarget_ = {};
    gateEntryCameraEndEye_ = {};
    gateEntryCameraEndTarget_ = {};
    gateEntryHadPlayerControl_ = player_ ? player_->IsControlActive() : true;

    if (gateEntryTargetGate_ && player_) {
        const engine::Vector3 gatePos = gateEntryTargetGate_->GetWorldPosition();
        const engine::Vector3 toGateRaw = {gatePos.x - gateEntryStartPlayerPosition_.x, 0.0f,
                                   gatePos.z - gateEntryStartPlayerPosition_.z};
        // 接触位置そのものではなく、ゲート面の向きから突入方向を決める。
        // これにより、ゲートの端から触れてもカメラ構図と突入方向が大きく暴れない。
        const engine::Vector3 toGate = SelectGateEntryDirection(gateEntryTargetGate_, toGateRaw);
        gateEntryDirection_ = toGate;
        const float entryY = gatePos.y;
        const SelectGateEntryRoute entryRoute = SelectBuildGateEntryRoute(gatePos, toGate, entryY);
        gateEntrySurfacePlayerPosition_ = entryRoute.surface;
        gateEntryTargetPlayerPosition_ = entryRoute.center;
        gateEntryInsidePlayerPosition_ = entryRoute.inside;
        player_->SetMoveYaw(std::atan2(toGate.x, toGate.z));
        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
        player_->SetSlimeAnimationDirection(toGate);
        player_->TriggerSlimeImpulse({1.08f, 0.92f, 1.10f}, 0.20f);
        CaptureGateEntryMaterialState(player_);
        ApplyGateEntryDissolveMaterial(0.0f, gatePos, toGate);
        if (auto* stageGate = dynamic_cast<GimmickStageGate*>(gateEntryTargetGate_)) {
            stageGate->TriggerEntryReaction();
        }

        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            const engine::Vector3 side = {toGate.z, 0.0f, -toGate.x};
            // カメラはプレイヤーの接触位置ではなくゲート中心を基準にする。
            // 表裏の差はtoGateの向きだけで反転させ、どちら側から入っても同じ構図に近づける。
            const engine::Vector3 focus = {gatePos.x, gatePos.y + kGateEntryCameraFocusHeight, gatePos.z};
            gateEntryCameraStartEye_ = camera->GetEye();
            gateEntryCameraStartTarget_ = camera->GetTargetPoint();
            gateEntryCameraEndTarget_ = focus;
            gateEntryCameraEndEye_ = {
                focus.x - toGate.x * kGateEntryCameraBackDistance + side.x * kGateEntryCameraSideOffset,
                focus.y + kGateEntryCameraHeight,
                focus.z - toGate.z * kGateEntryCameraBackDistance + side.z * kGateEntryCameraSideOffset};
            camera->SetInputEnabled(false);
            camera->SetFollowTarget(nullptr);
            camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
            camera->ConfigFixedPoint(gateEntryCameraStartEye_,
                                     SelectLookAtRotation(gateEntryCameraStartEye_, gateEntryCameraStartTarget_));
        }
    }

    gateEntryCinematicActive_ = true;
    gateEntryCinematicTimer_ = 0.0f;
    gateEntryPendingStageIndex_ = stageIndex;
    isChangingStage_ = true;
    stageDecisionCooldown_ = 0.35f;
    if (gatePromptSprite_) {
        gatePromptSprite_->SetVisible(false);
        gatePromptSprite_->Update();
    }
    DebugConsole::GetInstance()->AddLog("Stage Select: gate entry cinematic start.");
}

// 入口へ向かう移動とカメラ演出を進め、完了後に指定ステージへ切り替えます。
void GameSelectScene::UpdateGateEntryCinematic(float deltaTime) {
    if (!gateEntryCinematicActive_) {
        return;
    }

    gateEntryCinematicTimer_ += deltaTime;
    const float t = SelectClamp01(gateEntryCinematicTimer_ / kGateEntryCinematicDuration);
    const float cameraT = SelectEaseInOutCubic(SelectClamp01(gateEntryCinematicTimer_ / kGateEntryCameraBlendTime));
    const float alignT = SelectEaseOutCubic(SelectClamp01(gateEntryCinematicTimer_ / kGateEntryAlignEndTime));
    const float centeringT =
        SelectEaseInOutCubic(SelectClamp01((t - kGateEntryCenteringDelay) / kGateEntryCenteringDuration));
    const float diveT = SelectEaseInOutCubic(SelectClamp01(
        (t - kGateEntryDiveStartTime) / std::max(kGateEntryDiveEndTime - kGateEntryDiveStartTime, 0.001f)));
    const float clipT = SelectEaseInOutCubic(SelectClamp01(
        (t - kGateEntryDissolveStartTime) / std::max(kGateEntryDissolveEndTime - kGateEntryDissolveStartTime, 0.001f)));

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetInputEnabled(false);
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
        const engine::Vector3 currentEye = SelectLerpVector3(gateEntryCameraStartEye_, gateEntryCameraEndEye_, cameraT);
        const engine::Vector3 currentTarget =
            SelectLerpVector3(gateEntryCameraStartTarget_, gateEntryCameraEndTarget_, cameraT);
        camera->ConfigFixedPoint(currentEye, SelectLookAtRotation(currentEye, currentTarget));
    }

    if (player_) {
        player_->SetIsVisible(true);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
        player_->SetIsControlActive(false);

        if (gateEntryTargetGate_) {
            const engine::Vector3 gatePos = gateEntryTargetGate_->GetWorldPosition();
            const engine::Vector3 toGate = gateEntryDirection_;
            const float entryY = gatePos.y;
            // 演出中にゲートが親子関係やEditor操作で動いても、入口・中心・奥位置を最新Transformから作り直す。
            const SelectGateEntryRoute entryRoute = SelectBuildGateEntryRoute(gatePos, toGate, entryY);
            gateEntrySurfacePlayerPosition_ = entryRoute.surface;
            gateEntryTargetPlayerPosition_ = entryRoute.center;
            gateEntryInsidePlayerPosition_ = entryRoute.inside;
            player_->SetMoveYaw(std::atan2(toGate.x, toGate.z));
            player_->SetSlimeAnimationDirection(toGate);
            ApplyGateEntryDissolveMaterial(clipT, gatePos, toGate);
        }

        // 接触した瞬間の位置から直接ゲート中心へ飛ばすとワープ感が強いので、
        // まずゲート正面の入口ラインへ短く補間し、その後ゲート奥へ吸い込む。
        engine::Vector3 centeredStart = gateEntryStartPlayerPosition_;
        centeredStart.y = gateEntryStartPlayerPosition_.y +
                          (gateEntrySurfacePlayerPosition_.y - gateEntryStartPlayerPosition_.y) * centeringT;
        const engine::Vector3 alignedStart = SelectLerpVector3(centeredStart, gateEntrySurfacePlayerPosition_, alignT);
        const engine::Vector3 controlA =
            SelectLerpVector3(gateEntrySurfacePlayerPosition_, gateEntryTargetPlayerPosition_, 0.42f);
        const engine::Vector3 controlB =
            SelectLerpVector3(gateEntryTargetPlayerPosition_, gateEntryInsidePlayerPosition_, 0.58f);
        const engine::Vector3 divePos = SelectBezierVector3(
            gateEntrySurfacePlayerPosition_, {controlA.x, controlA.y + kGateEntryLiftHeight, controlA.z},
            {controlB.x, controlB.y + kGateEntryLiftHeight * 0.35f, controlB.z}, gateEntryInsidePlayerPosition_, diveT);
        engine::Vector3 playerPos = SelectLerpVector3(alignedStart, divePos, SelectClamp01((t - 0.08f) / 0.28f));

        player_->SetTranslate(playerPos);
        player_->SetScale(gateEntryStartPlayerScale_);
        if (clipT >= 0.995f || t >= 0.94f) {
            player_->SetIsVisible(false);
        }
    }

    if (gateEntryCinematicTimer_ >= kGateEntryCinematicDuration) {
        if (player_) {
            const engine::Vector3 gatePos =
                gateEntryTargetGate_ ? gateEntryTargetGate_->GetWorldPosition() : gateEntryInsidePlayerPosition_;
            ApplyGateEntryDissolveMaterial(1.0f, gatePos, gateEntryDirection_);
            gateEntryMaterialSnapshots_.clear();
        }
        gateEntryCinematicActive_ = false;
        gateEntryCinematicTimer_ = 0.0f;
        gateEntryPendingStageIndex_ = -1;
        gateEntryTargetGate_ = nullptr;
        ResetGateCinematicBars();
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
    }
}

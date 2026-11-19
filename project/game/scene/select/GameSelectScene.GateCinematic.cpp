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

    // 複数の演出が続いても、最初に上書きしたときだけ元の高さを退避します。
    const bool isCinematicActive =
        gateEntryCinematicActive_ || gateReturnPresentationActive_ || unlockingStageIndex_ >= 0;
    if (isCinematicActive && !gateCinematicBarOverrideActive_) {
        gateCinematicBarBaseHeight_ = params->cinemaBarHeight;
        gateCinematicBarOverrideActive_ = true;
    }

    if (!gateCinematicBarOverrideActive_) {
        return;
    }

    // 演出中と終了後で開閉速度を切り替え、元の高さより小さくしません。
    const float targetBlend = isCinematicActive ? 1.0f : 0.0f;
    const float speed = isCinematicActive ? kGateCinematicBarOpenSpeed : kGateCinematicBarCloseSpeed;
    const float step = speed * std::max(deltaTime, 0.0f);

    if (gateCinematicBarBlend_ < targetBlend) {
        gateCinematicBarBlend_ = std::min(targetBlend, gateCinematicBarBlend_ + step);
    } else if (gateCinematicBarBlend_ > targetBlend) {
        gateCinematicBarBlend_ = std::max(targetBlend, gateCinematicBarBlend_ - step);
    }

    // 帯の高さは画面比率。設定の高さから演出用の高さまでを0〜1の進行率で補間する。
    const float targetHeight = std::max(gateCinematicBarBaseHeight_, kGateCinematicBarHeight);
    params->cinemaBarHeight =
        gateCinematicBarBaseHeight_ + (targetHeight - gateCinematicBarBaseHeight_) * gateCinematicBarBlend_;

    // 閉じ切ったところで設定を返し、次の演出が始まるまで黒帯を書き換えません。
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
    // シーンからの帰還要求が無い場合と、進入演出と競合する場合は再生しません。
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

    // ゲートの向きから外側へ向かう経路を作り、着地の地面高さを別に調べます。
    const engine::Vector3 gatePos = gateReturnTargetGate_->GetWorldPosition();
    const engine::Vector3 selectPlayerStart = player_->GetWorldPosition();
    const engine::Vector3 stageNodePosition = GetStageNodePosition(request.stageIndex);
    // 門の軸と符号を選ぶ基準に節点から門への向きを使う。同じ座標ならプレイヤーから門への向きを使う。
    const engine::Vector3 fallbackToGate = {gatePos.x - stageNodePosition.x, 0.0f, gatePos.z - stageNodePosition.z};
    const float fallbackLengthSq = fallbackToGate.x * fallbackToGate.x + fallbackToGate.z * fallbackToGate.z;
    const engine::Vector3 fallbackFromPlayer = {gatePos.x - selectPlayerStart.x, 0.0f, gatePos.z - selectPlayerStart.z};
    const engine::Vector3 gateDirection = SelectGateEntryDirection(
        gateReturnTargetGate_, fallbackLengthSq > 0.0001f ? fallbackToGate : fallbackFromPlayer);
    // 帰還は入場ルートと反対へ進む。横方向だけ反転させ、着地高さは床の探索へ任せる。
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

    // 帰還したステージを選択し、開始前の大きさと操作状態を後の復帰用に記録します。
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

    // プレイヤーが使う経路には門の内側・中心・着地点をワールド座標で渡す。
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

    // 現視点、演出の注視点、着地後の通常視点を別々に用意し、後で補間します。
    if (engine::Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
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
        // 復帰用の目の位置を着地点の背面に作り、演出終了後の三人称視点へ近づけておく。
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
        camera->SetFollowMode(engine::Camera::FollowMode::FixedPoint);
        camera->ConfigFixedPoint(gateReturnCameraStartEye_,
                                 SelectLookAtRotation(gateReturnCameraStartEye_, gateReturnCameraStartTarget_));
    }

    // 再生時間を初期化し、演出の全尺をゲートの再決定待ちに使います。
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

    // カメラ補間は帰還開始からの経過秒で進める。各フレームの時刻をプレイヤーの姿勢に直接書き込まない。
    gateReturnPresentationTimer_ += deltaTime;
    const engine::Vector3 outwardDirection =
        SelectNormalizeGateDirectionXZ({-gateReturnDirection_.x, 0.0f, -gateReturnDirection_.z}, {0.0f, 0.0f, -1.0f});

    // 配置ゲートが残っていれば現在位置から経路を引き直し、床の高さを確認します。
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

    if (engine::Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetInputEnabled(false);
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(engine::Camera::FollowMode::FixedPoint);

        engine::Vector3 eye = gateReturnCameraEndEye_;
        engine::Vector3 target = gateReturnCameraEndTarget_;

        // 導入は現在視点からゲート側へ寄せ、後半は着地点からの追従位置へ移します。
        if (gateReturnPresentationTimer_ < kGateReturnCameraRestoreStartTime) {
            const float introT =
                SelectEaseOutCubic(SelectClamp01(gateReturnPresentationTimer_ / kGateReturnCameraIntroDuration));
            eye = SelectLerpVector3(gateReturnCameraStartEye_, gateReturnCameraEndEye_, introT);
            target = SelectLerpVector3(gateReturnCameraStartTarget_, gateReturnCameraEndTarget_, introT);
        } else {
            // 復帰開始時刻を引いて補間率を0から数え直し、視点と注視点を同じ曲線で動かす。
            const float restoreT = SelectEaseInOutCubic(SelectClamp01(
                (gateReturnPresentationTimer_ - kGateReturnCameraRestoreStartTime) / kGateReturnCameraRestoreDuration));
            eye = SelectLerpVector3(gateReturnCameraEndEye_, gateReturnCameraRestoreEye_, restoreT);
            target = SelectLerpVector3(gateReturnCameraEndTarget_, gateReturnCameraRestoreTarget_, restoreT);
        }

        camera->ConfigFixedPoint(eye, SelectLookAtRotation(eye, target));
    }

    // 帰還アニメーションだけがプレイヤーを動かすよう、操作と通常速度を止めます。
    if (player_) {
        player_->SetIsVisible(true);
        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    // プレイヤーの動作が完了した場合、または演出の全尺に達した場合に通常制御へ戻します。
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

    // 帰還前の操作可否を戻し、待機姿勢と着地の反応へ引き継ぎます。
    if (player_) {
        player_->StopGateReturnAnimation(gateReturnHadPlayerControl_);
        player_->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
        player_->TriggerSlimeImpulse({0.96f, 1.08f, 0.96f}, 0.18f);
    }

    // 固定カメラを解除し、プレイヤーの追従を再開できる位置に合わせます。
    if (engine::Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        if (player_) {
            camera->SetFollowTarget(player_);
            camera->SetFollowMode(engine::Camera::FollowMode::Aimable);
            camera->SnapToThirdPerson(kGateReturnThirdPersonDistance, kGateReturnThirdPersonHeight,
                                      kGateReturnThirdPersonPitch);
        }

        camera->SetInputEnabled(true);
    }

    // 帰還に続く報酬演出を優先し、報酬がなければ未再生の解放演出を探します。
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
    // 実行中の配置からゲートを探し、共通演出の開始に成功した場合だけ進入中のフラグを立てる。
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
    // 移動やカメラは再生器に任せ、再生終了を確認してから次のシーンへ進む。
    gateEntryPresentation_.Update(deltaTime);
    if (!gateEntryPresentation_.IsFinished()) return;
    // 参照と画面の帯を先に片付け、ゲームプレイへの遷移を一度だけ要求する。
    gateEntryCinematicActive_ = false;
    gateEntryTargetGate_ = nullptr;
    ResetGateCinematicBars();
    SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
}

#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include "Camera.h"
#include "Player.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "StageManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "GameAudioSettings.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include "game/scene/select/GameSelectScene.Internal.h"
using namespace StageSelectDetail;

// 解放演出の開始・進行・復帰。入力とカメラの復帰、既読保存の条件を一組として維持する。
void GameSelectScene::BeginUnlockPresentation() {
    // 鍵・錠・カメラの元状態と操作権を保存してから、解放ムービーへ移る。
    if (unlockingStageIndex_ < 0 || unlockPresentationInitialized_) {
        return;
    }

    unlockPresentationInitialized_ = true;
    unlockKeyAppearImpactPlayed_ = false;
    unlockTurnImpactPlayed_ = false;
    unlockReleaseImpactPlayed_ = false;
    unlockGateActivationImpactPlayed_ = false;
    unlockTargetGate_ = FindStageGateByIndex(unlockingStageIndex_);
    unlockKeyObject_ = FindObjectByName(std::string(kStageGateCrownKeyPrefix) + std::to_string(unlockingStageIndex_));
    unlockLockObject_ = FindObjectByName(std::string(kStageGateLockPrefix) + std::to_string(unlockingStageIndex_));

    // ゲートが見つからない場合もステージ配置座標を基準に演出位置を計算する。
    const engine::Vector3 gatePosition =
        unlockTargetGate_ ? unlockTargetGate_->GetWorldPosition() : GetStageNodePosition(unlockingStageIndex_);
    const engine::Vector3 towardHub = {-gatePosition.x, 0.0f, -gatePosition.z};
    unlockGateDirection_ = SelectGateEntryDirection(unlockTargetGate_, towardHub);

    if (unlockKeyObject_) {
        unlockKeyBasePosition_ = unlockKeyObject_->GetTranslate();
        unlockKeyBaseRotation_ = unlockKeyObject_->GetRotation();
        unlockKeyBaseScale_ = unlockKeyObject_->GetScale();
        // 正面カメラが閉じた南京錠を見せ終えるまでは、王冠鍵を画面へ出しません。
        unlockKeyObject_->SetIsVisible(false);
        unlockKeyObject_->SetColor({1.0f, 0.84f, 0.24f, 1.0f});
        unlockKeyObject_->SetEmissive(2.8f);
        unlockKeyObject_->SetGPUParticleName("");
    }

    if (unlockLockObject_) {
        unlockLockBasePosition_ = unlockLockObject_->GetTranslate();
        unlockLockBaseRotation_ = unlockLockObject_->GetRotation();
        unlockLockBaseScale_ = unlockLockObject_->GetScale();
        unlockLockObject_->SetIsVisible(true);
    }

    if (player_) {
        unlockHadPlayerControl_ = player_->IsControlActive();
        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    // 注視点は錠、ゲート、ステージ節点の優先順で選び、元の視点を復帰用に保存する。
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        engine::Vector3 focus = gatePosition + engine::Vector3{0.0f, 1.10f, 0.0f};
        if (unlockTargetGate_) {
            const AABB gateBounds = unlockTargetGate_->GetAABB();
            focus = (gateBounds.min + gateBounds.max) * 0.5f;
        }
        if (unlockLockObject_) {
            const AABB lockBounds = unlockLockObject_->GetAABB();
            focus = (lockBounds.min + lockBounds.max) * 0.5f;
        }
        unlockCameraStartEye_ = camera->GetEye();
        unlockCameraStartTarget_ = camera->GetTargetPoint();
        unlockCameraCinematicTarget_ = focus + engine::Vector3{0.0f, 0.08f, 0.0f};
        unlockCameraCinematicEye_ = {focus.x + unlockGateDirection_.x * 8.4f, focus.y + 1.10f,
                                     focus.z + unlockGateDirection_.z * 8.4f};
        camera->SetInputEnabled(false);
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
        camera->ConfigFixedPoint(unlockCameraStartEye_,
                                 SelectLookAtRotation(unlockCameraStartEye_, unlockCameraStartTarget_));
    }

    stageDecisionCooldown_ = kUnlockPresentationDuration + 0.25f;
    DebugConsole::GetInstance()->AddLog("Stage Select: crown key unlock cinematic start.");
}

#ifdef USE_IMGUI
void GameSelectScene::StartDebugUnlockPresentation(int stageIndex) {
    // セーブの既読状態を変えず、必要なモデルが揃う場合だけ演出を試す。
    const auto& stages = StageManager::GetInstance()->GetStages();
    if (stageIndex <= 0 || stageIndex >= static_cast<int>(stages.size())) {
        return;
    }
    if (gateEntryCinematicActive_ || gateReturnPresentationActive_ || crownCountPresentationActive_ ||
        unlockingStageIndex_ >= 0) {
        return;
    }

    engine::Object3d* gate = FindStageGateByIndex(stageIndex);
    engine::Object3d* key = FindObjectByName(std::string(kStageGateCrownKeyPrefix) + std::to_string(stageIndex));
    engine::Object3d* lock = FindObjectByName(std::string(kStageGateLockPrefix) + std::to_string(stageIndex));
    if (!gate || !key || !lock) {
        DebugConsole::GetInstance()->AddLog("Stage Select: debug crown unlock cinematic assets are missing.");
        return;
    }

    unlockDebugPreviousSelectedStageIndex_ = selectedStageIndex_;
    unlockPresentationDebugReplay_ = true;
    unlockPresentationInitialized_ = false;
    unlockKeyAppearImpactPlayed_ = false;
    unlockTurnImpactPlayed_ = false;
    unlockReleaseImpactPlayed_ = false;
    unlockGateActivationImpactPlayed_ = false;
    unlockingStageIndex_ = stageIndex;
    unlockPresentationTimer_ = 0.0f;
    unlockParticleTimer_ = 0.0f;
    selectedStageIndex_ = stageIndex;
    ApplyStageGateStates();
    DebugConsole::GetInstance()->AddLog("Stage Select: debug crown unlock cinematic requested.");
}
#endif

void GameSelectScene::UpdateUnlockPresentation(float deltaTime) {
    // 鍵の出現・挿入・回転・錠の落下・ゲート点灯を同じ時計で同期する。
    if (unlockingStageIndex_ < 0) {
        return;
    }

    BeginUnlockPresentation();
    unlockPresentationTimer_ += deltaTime;
    unlockParticleTimer_ -= deltaTime;
    selectedStageIndex_ = unlockingStageIndex_;
    const float time = unlockPresentationTimer_;
    const engine::Vector3 side = {unlockGateDirection_.z, 0.0f, -unlockGateDirection_.x};
    const engine::Vector3 lockPosition = unlockLockObject_
                                     ? unlockLockBasePosition_
                                     : GetStageNodePosition(unlockingStageIndex_) + engine::Vector3{0.0f, 0.35f, 0.0f};
    engine::Vector3 gateCorePosition = lockPosition;
    if (unlockTargetGate_) {
        const AABB gateBounds = unlockTargetGate_->GetAABB();
        gateCorePosition = (gateBounds.min + gateBounds.max) * 0.5f;
    }

    if (player_) {
        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    // カメラは導入で錠の正面へ寄せ、終了前に開始位置へ補間して戻す。
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetInputEnabled(false);
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
        engine::Vector3 eye = unlockCameraCinematicEye_;
        engine::Vector3 target = unlockCameraCinematicTarget_;
        if (time < kUnlockCameraIntroDuration) {
            const float cameraT = SelectEaseInOutCubic(SelectClamp01(time / kUnlockCameraIntroDuration));
            eye = SelectLerpVector3(unlockCameraStartEye_, unlockCameraCinematicEye_, cameraT);
            target = SelectLerpVector3(unlockCameraStartTarget_, unlockCameraCinematicTarget_, cameraT);
        } else if (time >= kUnlockCameraRestoreStartTime) {
            const float restoreT = SelectEaseInOutCubic(
                SelectClamp01((time - kUnlockCameraRestoreStartTime) / kUnlockCameraRestoreDuration));
            eye = SelectLerpVector3(unlockCameraCinematicEye_, unlockCameraStartEye_, restoreT);
            target = SelectLerpVector3(unlockCameraCinematicTarget_, unlockCameraStartTarget_, restoreT);
        }
        camera->ConfigFixedPoint(eye, SelectLookAtRotation(eye, target));
    }

    // 鍵は登場、曲線での接近、回転、離脱を同じ時間軸上で組み立てる。
    if (unlockKeyObject_) {
        const engine::Vector3 startPosition = {lockPosition.x + unlockGateDirection_.x * 5.45f + side.x * 1.30f,
                                       lockPosition.y + 1.65f,
                                       lockPosition.z + unlockGateDirection_.z * 5.45f + side.z * 1.30f};
        const engine::Vector3 insertPosition = {lockPosition.x + unlockGateDirection_.x * 0.88f, lockPosition.y,
                                        lockPosition.z + unlockGateDirection_.z * 0.88f};
        const float appearT =
            SelectEaseOutBack(SelectClamp01((time - kUnlockKeyAppearStartTime) / kUnlockKeyAppearDuration));
        const float approachT = SelectEaseInOutCubic(
            SelectClamp01((time - kUnlockKeyApproachStartTime) /
                          std::max(kUnlockKeyApproachEndTime - kUnlockKeyApproachStartTime, 0.001f)));
        const engine::Vector3 controlPosition = {(startPosition.x + insertPosition.x) * 0.5f + side.x * 1.15f,
                                         std::max(startPosition.y, insertPosition.y) + 1.25f,
                                         (startPosition.z + insertPosition.z) * 0.5f + side.z * 1.15f};
        // 二回の線形補間で二次ベジェ曲線上の接近位置を求める。
        const engine::Vector3 firstCurve = SelectLerpVector3(startPosition, controlPosition, approachT);
        const engine::Vector3 secondCurve = SelectLerpVector3(controlPosition, insertPosition, approachT);
        engine::Vector3 keyPosition = SelectLerpVector3(firstCurve, secondCurve, approachT);
        float keyScaleRate = appearT;
        float keyRoll = (1.0f - approachT) * std::sin(time * 8.0f) * 0.16f;
        // 錠に差し込んだ後は位置を固定し、回転だけを進める。
        if (time >= kUnlockKeyTurnStartTime) {
            const float turnT = SelectEaseInOutCubic(SelectClamp01(
                (time - kUnlockKeyTurnStartTime) / std::max(kUnlockKeyTurnEndTime - kUnlockKeyTurnStartTime, 0.001f)));
            keyPosition = insertPosition;
            keyRoll = turnT * std::numbers::pi_v<float> * 0.5f;
            keyScaleRate *= 1.0f + std::sin(turnT * std::numbers::pi_v<float>) * 0.12f;
        }
        // 開錠後は鍵を上へ逃がしながら縮小する。
        if (time >= kUnlockLockReleaseTime) {
            const float vanishT = SelectEaseInOutCubic(SelectClamp01((time - kUnlockLockReleaseTime) / 0.88f));
            keyPosition.x += unlockGateDirection_.x * vanishT * 0.75f;
            keyPosition.y += vanishT * 1.85f;
            keyPosition.z += unlockGateDirection_.z * vanishT * 0.75f;
            keyScaleRate *= 1.0f - vanishT;
            keyRoll += vanishT * 0.55f;
        }

        unlockKeyObject_->SetTranslate(keyPosition);
        unlockKeyObject_->SetRotation({0.0f, std::atan2(unlockGateDirection_.x, unlockGateDirection_.z), keyRoll});
        unlockKeyObject_->SetScale({unlockKeyBaseScale_.x * std::max(keyScaleRate, 0.001f),
                                    unlockKeyBaseScale_.y * std::max(keyScaleRate, 0.001f),
                                    unlockKeyBaseScale_.z * std::max(keyScaleRate, 0.001f)});
        unlockKeyObject_->SetIsVisible(time >= kUnlockKeyAppearStartTime && time < kUnlockLockReleaseTime + 0.88f);
        unlockKeyObject_->SetEmissive(2.7f + std::sin(time * 9.0f) * 0.35f);
    }

    // 錠は鍵の回転に反応して揺れ、解放時に落下して画面から外れる。
    if (unlockLockObject_) {
        engine::Vector3 position = unlockLockBasePosition_;
        engine::Vector3 rotation = unlockLockBaseRotation_;
        engine::Vector3 scale = unlockLockBaseScale_;
        if (time >= kUnlockKeyTurnStartTime && time < kUnlockLockReleaseTime) {
            const float shakeT = SelectClamp01((time - kUnlockKeyTurnStartTime) /
                                               std::max(kUnlockLockReleaseTime - kUnlockKeyTurnStartTime, 0.001f));
            const float shake = std::sin(time * 42.0f) * 0.10f * shakeT;
            position.x += side.x * shake;
            position.z += side.z * shake;
            scale = {unlockLockBaseScale_.x * (1.0f + shakeT * 0.06f), unlockLockBaseScale_.y * (1.0f - shakeT * 0.03f),
                     unlockLockBaseScale_.z * (1.0f + shakeT * 0.06f)};
        }
        if (time >= kUnlockLockReleaseTime) {
            const float dropT = SelectEaseInOutCubic(SelectClamp01(
                (time - kUnlockLockReleaseTime) / std::max(kUnlockLockHideTime - kUnlockLockReleaseTime, 0.001f)));
            position.x += side.x * dropT * 0.78f;
            position.y -= dropT * 3.2f;
            position.z += side.z * dropT * 0.78f;
            rotation.y += dropT * 0.45f;
            rotation.z -= dropT * 1.42f;
            const float scaleRate = 1.0f - dropT * 0.46f;
            scale = {unlockLockBaseScale_.x * scaleRate, unlockLockBaseScale_.y * scaleRate,
                     unlockLockBaseScale_.z * scaleRate};
        }
        unlockLockObject_->SetTranslate(position);
        unlockLockObject_->SetRotation(rotation);
        unlockLockObject_->SetScale(scale);
    }

    // 各節目の効果はフラグで一回だけ発火させ、フレーム落ちでも二重再生しない。
    if (!unlockKeyAppearImpactPlayed_ && time >= kUnlockKeyAppearStartTime) {
        unlockKeyAppearImpactPlayed_ = true;
        const engine::Vector3 keyPosition =
            unlockKeyObject_ ? unlockKeyObject_->GetTranslate() : lockPosition + unlockGateDirection_ * 4.0f;
        MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath, keyPosition,
                                                        {0.0f, 0.0f, 0.0f}, {0.30f, 0.30f, 0.30f});
        GPUParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, keyPosition);
    }

    if (!unlockTurnImpactPlayed_ && time >= kUnlockKeyTurnStartTime) {
        unlockTurnImpactPlayed_ = true;
        MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath, lockPosition,
                                                        {0.0f, 0.0f, 0.0f}, {0.34f, 0.34f, 0.34f});
        GPUParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, lockPosition);
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->StartShake(0.10f, 0.025f, 28.0f, {0.45f, 0.45f, 0.16f});
        }
    }

    if (!unlockReleaseImpactPlayed_ && time >= kUnlockLockReleaseTime) {
        unlockReleaseImpactPlayed_ = true;
        MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath, lockPosition,
                                                        {0.0f, 0.0f, 0.0f}, {0.58f, 0.58f, 0.58f});
        GPUParticleManager::GetInstance()->Emit(kCrownUnlockParticlePreset, lockPosition);
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->StartShake(0.22f, 0.065f, 24.0f, {0.62f, 0.50f, 0.18f});
        }
    }

    if (!unlockGateActivationImpactPlayed_ && time >= kUnlockGateActivationStartTime) {
        unlockGateActivationImpactPlayed_ = true;
        GPUParticleManager::GetInstance()->Emit(kGateUnlockCoreParticlePreset, gateCorePosition);
        GPUParticleManager::GetInstance()->Emit(kGateUnlockSparkParticlePreset, gateCorePosition);
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->StartShake(0.16f, 0.035f, 22.0f, {0.42f, 0.42f, 0.20f});
        }
    }

    // 接近中の残光だけは一定間隔で追加し、節目の単発演出と分ける。
    if (unlockParticleTimer_ <= 0.0f && time >= kUnlockKeyAppearStartTime && time < kUnlockLockReleaseTime) {
        unlockParticleTimer_ = 0.15f;
        const engine::Vector3 particlePosition = unlockKeyObject_ ? unlockKeyObject_->GetTranslate() : lockPosition;
        GPUParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, particlePosition);
    }

    if (time >= kUnlockPresentationDuration) {
        FinishUnlockPresentation();
    }
}

void GameSelectScene::FinishUnlockPresentation() {
    // デバッグ再生時は既読にせず、鍵・錠・カメラと操作権を開始前へ戻す。
    if (unlockingStageIndex_ < 0) {
        return;
    }

    const int unlockedStageIndex = unlockingStageIndex_;
    const bool debugReplay = unlockPresentationDebugReplay_;
    if (!debugReplay) {
        GameDataManager::GetInstance()->MarkStageUnlockSeen(unlockedStageIndex);
    }

    // 演出で動かした鍵・錠を元のTransformへ戻し、表示状態はゲート状態の再適用に委ねる。
    if (unlockKeyObject_) {
        unlockKeyObject_->SetTranslate(unlockKeyBasePosition_);
        unlockKeyObject_->SetRotation(unlockKeyBaseRotation_);
        unlockKeyObject_->SetScale(unlockKeyBaseScale_);
        unlockKeyObject_->SetIsVisible(false);
        unlockKeyObject_->SetGPUParticleName("");
    }
    if (unlockLockObject_) {
        unlockLockObject_->SetTranslate(unlockLockBasePosition_);
        unlockLockObject_->SetRotation(unlockLockBaseRotation_);
        unlockLockObject_->SetScale(unlockLockBaseScale_);
        unlockLockObject_->SetIsVisible(false);
    }
    if (player_) {
        player_->SetIsControlActive(unlockHadPlayerControl_);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }
    // カメラ入力とプレイヤー追従を復元し、演出開始前の操作権へ戻す。
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetEye(unlockCameraStartEye_);
        camera->SetTarget(unlockCameraStartTarget_);
        camera->SyncRotationToCurrentView();
        if (player_) {
            camera->SetFollowTarget(player_);
            camera->SetFollowMode(Camera::FollowMode::kAimable);
            camera->ResetFollowSmoothing();
        }
        camera->SetInputEnabled(true);
    }

    // 次の解放演出が前回の発火フラグや対象ポインタを引き継がないよう初期化する。
    unlockingStageIndex_ = -1;
    unlockPresentationTimer_ = 0.0f;
    unlockParticleTimer_ = 0.0f;
    unlockPresentationInitialized_ = false;
    unlockKeyAppearImpactPlayed_ = false;
    unlockTurnImpactPlayed_ = false;
    unlockReleaseImpactPlayed_ = false;
    unlockGateActivationImpactPlayed_ = false;
    unlockTargetGate_ = nullptr;
    unlockKeyObject_ = nullptr;
    unlockLockObject_ = nullptr;
    unlockPresentationDebugReplay_ = false;
    if (debugReplay && unlockDebugPreviousSelectedStageIndex_ >= 0) {
        selectedStageIndex_ = unlockDebugPreviousSelectedStageIndex_;
    }
    unlockDebugPreviousSelectedStageIndex_ = -1;
    stageDecisionCooldown_ = 0.35f;
    ApplyStageGateStates();
    DebugConsole::GetInstance()->AddLog("Stage Select: crown key unlock cinematic finished.");
}

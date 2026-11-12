#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include "Camera.h"
#include "Player.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "game/system/progress/StageManager.h"
#include "GpuParticleManager.h"
#include "MeshEffectManager.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>
#include "game/scene/select/GameSelectScene.Internal.h"
using namespace StageSelectDetail;

// 解放演出の開始・進行・復帰。入力とカメラの復帰、既読保存の条件を一組として維持する。
void GameSelectScene::BeginUnlockPresentation() {
    // Director に姿勢を記録させ、プレイヤーの操作権と視点を保存してから再生する。
    if (unlockingStageIndex_ < 0 || unlockPresentationInitialized_) {
        return;
    }

    unlockPresentationInitialized_ = true;
    unlockTargetGate_ = FindStageGateByIndex(unlockingStageIndex_);
    unlockKeyObject_ = FindObjectByName(std::string(kStageGateCrownKeyPrefix) + std::to_string(unlockingStageIndex_));
    unlockLockObject_ = FindObjectByName(std::string(kStageGateLockPrefix) + std::to_string(unlockingStageIndex_));

    if (unlockKeyObject_) {
        // 正面カメラが閉じた南京錠を見せ終えるまでは、王冠鍵を画面へ出しません。
        unlockKeyObject_->SetIsVisible(false);
        unlockKeyObject_->SetColor({1.0f, 0.84f, 0.24f, 1.0f});
        unlockKeyObject_->SetEmissive(2.8f);
        unlockKeyObject_->SetGpuParticleName("");
    }

    if (unlockLockObject_) {
        unlockLockBasePosition_ = unlockLockObject_->GetTranslate();
        unlockLockObject_->SetIsVisible(true);
    }

    // 動作と表示の節目は JSON の Signal だけから読み取る。
    unlockTimelineLoaded_ = unlockSequence_.Load("Resources/json/scenario/stage_select_unlock_" +
                                                 std::to_string(unlockingStageIndex_) + ".json");
    if (unlockTimelineLoaded_) {
        unlockPlayer_.Initialize(SceneManager::GetInstance());
        unlockPlayer_.SetBindingResolver({});
        unlockPlayer_.SetSequence(&unlockSequence_);
        unlockPlayer_.SetSignalCallback([this](engine::Object3D*, const CinematicSignalMarker& signal, bool isPreview) {
            if (!isPreview) {
                HandleUnlockSignal(signal);
            }
        });
        auto issues = unlockPlayer_.ValidateBindings();
        auto signalTime = [this, &issues](const char* name) {
            for (const auto& signal : unlockSequence_.signals) {
                if (signal.enabled && signal.signal == name) {
                    return signal.time;
                }
            }
            issues.emplace_back(std::string("必須 Signal がありません: ") + name);
            return 0.0f;
        };
        unlockKeyAppearTime_ = signalTime("key_appear");
        unlockKeyApproachEndTime_ = signalTime("key_insert");
        signalTime("key_turn");
        unlockLockReleaseTime_ = signalTime("lock_release");
        unlockKeyHideTime_ = signalTime("key_hide");
        unlockLockHideTime_ = signalTime("lock_hide");
        unlockGateActivationTime_ = signalTime("gate_activate");
        unlockGateActivationEndTime_ = signalTime("gate_active_full");
        const auto hasTrack = [this](const engine::Object3D* object) {
            return object && std::any_of(unlockSequence_.transformTracks.begin(), unlockSequence_.transformTracks.end(),
                                         [object](const CinematicTransformTrack& track) {
                                             return track.enabled && !track.muted &&
                                                    track.binding.targetName == object->GetName();
                                         });
        };
        if (!hasTrack(unlockKeyObject_) || !hasTrack(unlockLockObject_)) {
            issues.emplace_back("鍵または南京錠の Transform Track が対象ステージを指していません。");
        }
        const std::string cameraName = "StageSelect_UnlockCamera_" + std::to_string(unlockingStageIndex_);
        if (!std::any_of(unlockSequence_.cameraShots.begin(), unlockSequence_.cameraShots.end(),
                         [&cameraName](const CinematicCameraShot& shot) {
                             return shot.enabled && !shot.muted && shot.binding.targetName == cameraName;
                         })) {
            issues.emplace_back("解錠カメラの Shot が対象ステージを指していません。");
        }
        if (!issues.empty()) {
            unlockTimelineLoaded_ = false;
            for (const auto& issue : issues) {
                DebugConsole::GetInstance()->AddLog("Stage Select unlock timeline: " + issue);
            }
        }
    } else {
        DebugConsole::GetInstance()->AddLog("Stage Select: unlock timeline missing; presentation canceled for retry.");
    }

    if (player_) {
        unlockHadPlayerControl_ = player_->IsControlActive();
        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    // 再生前の視点を保存する。ムービー中の視点は JSON の Camera Track が制御する。
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        unlockCameraStartEye_ = camera->GetEye();
        unlockCameraStartTarget_ = camera->GetTargetPoint();
        camera->SetInputEnabled(false);
        camera->SetFollowTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::FixedPoint);
        camera->ConfigFixedPoint(unlockCameraStartEye_,
                                 SelectLookAtRotation(unlockCameraStartEye_, unlockCameraStartTarget_));
    }

    if (!unlockTimelineLoaded_) {
        // 壊れたシナリオで操作不能にならないよう中断し、既読にはせず次回入場時に再試行する。
        FinishUnlockPresentation(false);
        return;
    }

    unlockPlayer_.Play(false);
    stageDecisionCooldown_ = unlockPlayer_.GetDuration() + 0.25f;
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

    engine::Object3D* gate = FindStageGateByIndex(stageIndex);
    engine::Object3D* key = FindObjectByName(std::string(kStageGateCrownKeyPrefix) + std::to_string(stageIndex));
    engine::Object3D* lock = FindObjectByName(std::string(kStageGateLockPrefix) + std::to_string(stageIndex));

    if (!gate || !key || !lock) {
        DebugConsole::GetInstance()->AddLog("Stage Select: debug crown unlock cinematic assets are missing.");
        return;
    }

    unlockDebugPreviousSelectedStageIndex_ = selectedStageIndex_;
    unlockPresentationDebugReplay_ = true;
    unlockPresentationInitialized_ = false;
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
    if (unlockingStageIndex_ < 0) {
        return;
    }
    unlockPlayer_.Update(deltaTime);
    unlockPresentationTimer_ = unlockPlayer_.GetCurrentTime();
    unlockParticleTimer_ -= deltaTime;
    selectedStageIndex_ = unlockingStageIndex_;
    const float time = unlockPresentationTimer_;
    const engine::Vector3 lockPosition = unlockLockObject_
                                     ? unlockLockBasePosition_
                                     : GetStageNodePosition(unlockingStageIndex_) + engine::Vector3{0.0f, 0.35f, 0.0f};

    if (player_) {
        player_->SetIsControlActive(false);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    if (unlockKeyObject_) {
        unlockKeyObject_->SetEmissive(2.7f + std::sin(time * 9.0f) * 0.35f);
    }

    // 接近中の残光だけは一定間隔で追加し、節目の単発演出と分ける。
    if (unlockParticleTimer_ <= 0.0f && time >= unlockKeyAppearTime_ && time < unlockLockReleaseTime_) {
        unlockParticleTimer_ = 0.15f;
        const engine::Vector3 particlePosition = unlockKeyObject_ ? unlockKeyObject_->GetTranslate() : lockPosition;
        GpuParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, particlePosition);
    }

    if (time >= unlockPlayer_.GetDuration()) {
        FinishUnlockPresentation();
    }
}

void GameSelectScene::HandleUnlockSignal(const CinematicSignalMarker& signal) {
    // 一度通過した Signal は Director が再通知しないため、シーン側の発火済みフラグは不要。
    const engine::Vector3 lockPosition = unlockLockObject_
                                     ? unlockLockBasePosition_
                                     : GetStageNodePosition(unlockingStageIndex_) + engine::Vector3{0.0f, 0.35f, 0.0f};

    if (signal.signal == "key_appear") {
        const engine::Vector3 position = unlockKeyObject_ ? unlockKeyObject_->GetTranslate() : lockPosition;
        MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath, position,
                                                        {0.0f, 0.0f, 0.0f}, {0.30f, 0.30f, 0.30f});
        GpuParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, position);
    } else if (signal.signal == "key_turn") {
        MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath, lockPosition,
                                                        {0.0f, 0.0f, 0.0f}, {0.34f, 0.34f, 0.34f});
        GpuParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, lockPosition);
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->StartShake(0.10f, 0.025f, 28.0f, {0.45f, 0.45f, 0.16f});
        }
    } else if (signal.signal == "lock_release") {
        MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath, lockPosition,
                                                        {0.0f, 0.0f, 0.0f}, {0.58f, 0.58f, 0.58f});
        GpuParticleManager::GetInstance()->Emit(kCrownUnlockParticlePreset, lockPosition);
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->StartShake(0.22f, 0.065f, 24.0f, {0.62f, 0.50f, 0.18f});
        }
    } else if (signal.signal == "gate_activate") {
        engine::Vector3 position = lockPosition;
        if (unlockTargetGate_) {
            const Aabb bounds = unlockTargetGate_->GetAabb();
            position = (bounds.min + bounds.max) * 0.5f;
        }
        GpuParticleManager::GetInstance()->Emit(kGateUnlockCoreParticlePreset, position);
        GpuParticleManager::GetInstance()->Emit(kGateUnlockSparkParticlePreset, position);
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->StartShake(0.16f, 0.035f, 22.0f, {0.42f, 0.42f, 0.20f});
        }
    }
}

void GameSelectScene::FinishUnlockPresentation(bool markSeen) {
    // デバッグ再生時は既読にせず、鍵・錠・カメラと操作権を開始前へ戻す。
    if (unlockingStageIndex_ < 0) {
        return;
    }

    const int unlockedStageIndex = unlockingStageIndex_;
    const bool debugReplay = unlockPresentationDebugReplay_;

    RestoreUnlockPresentationState();

    if (markSeen && !debugReplay) {
        GameDataManager::GetInstance()->MarkStageUnlockSeen(unlockedStageIndex);
    }

    // 次の解放演出へ対象ポインタと再生時計を引き継がないよう初期化する。
    unlockingStageIndex_ = -1;
    unlockPresentationTimer_ = 0.0f;
    unlockParticleTimer_ = 0.0f;
    unlockPresentationInitialized_ = false;
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

void GameSelectScene::RestoreUnlockPresentationState() {
    // Director が記録した姿勢を戻し、見た目は現在のゲート状態で更新する。
    if (unlockTimelineLoaded_) {
        unlockPlayer_.Stop(true);
    }
    unlockTimelineLoaded_ = false;
    if (unlockKeyObject_) {
        unlockKeyObject_->SetIsVisible(false);
        unlockKeyObject_->SetGpuParticleName("");
    }
    if (unlockLockObject_) {
        unlockLockObject_->SetIsVisible(false);
    }

    if (player_) {
        player_->SetIsControlActive(unlockHadPlayerControl_);
        player_->SetVelocity({0.0f, 0.0f, 0.0f});
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetEye(unlockCameraStartEye_);
        camera->SetTarget(unlockCameraStartTarget_);
        camera->SyncRotationToCurrentView();
        if (player_) {
            camera->SetFollowTarget(player_);
            camera->SetFollowMode(Camera::FollowMode::Aimable);
            camera->ResetFollowSmoothing();
        }
        camera->SetInputEnabled(true);
    }
}

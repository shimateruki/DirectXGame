#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "PostEffect.h"
#include "SceneManager.h"
#include "engine/utility/math/AnimationInterpolation.h"
#include "game/actor/gimmick/trigger/GimmickBossGate.h"
#include "game/actor/gimmick/trigger/GimmickArenaEncounter.h"
#include <algorithm>
#include <cmath>

#include "game/scene/gameplay/GamePlayScene.PresentationMath.h"
using namespace GamePlayPresentationMath;

namespace {
constexpr float kArenaBossIntroCinemaBarHeight = 0.115f;
constexpr float kArenaBossGameplayCameraDistance = 11.5f;
constexpr float kArenaBossGameplayCameraHeight = 3.8f;
constexpr float kArenaBossGameplayCameraPitch = 0.31415927f;
constexpr float kArenaBossRewardCinemaBarHeight = 0.115f;
constexpr const char* kArenaBossRewardSequencePath = "Resources/json/scenario/stage3_boss_reward.json";

// Arena内の演出で共通する操作停止と固定カメラへの切替。
bool BeginArenaFixedCamera(Player& player, bool preserveCopyAbility, bool& hadControl) {
    if (!CameraManager::GetInstance()->BeginMainCameraPresentation()) {
        return false;
    }
    hadControl = player.IsControlActive();
    player.BeginCinematicLock(preserveCopyAbility);
    player.SetVelocity({0.0f, 0.0f, 0.0f});
    return true;
}

void CaptureArenaCinemaBar(bool& active, float& baseHeight) {
    if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
        baseHeight = params->cinemaBarHeight;
        active = true;
    }
}

void UpdateArenaCinemaBar(float time, float openEnd, float closeStart, float endTime, float targetHeight,
                          float baseHeight) {
    PostEffect::Params* params = PostEffect::GetInstance()->GetParams();
    if (!params) {
        return;
    }

    const float openRate = AnimationInterpolation::ApplyEasing(
        AnimationInterpolation::SegmentRate(time, 0.0f, openEnd), AnimationInterpolation::EasingType::SmootherStep);
    const float closeRate = AnimationInterpolation::ApplyEasing(
        AnimationInterpolation::SegmentRate(time, closeStart, endTime),
        AnimationInterpolation::EasingType::SmootherStep);
    params->cinemaBarHeight = baseHeight +
                              ((std::max)(baseHeight, targetHeight) - baseHeight) * openRate * (1.0f - closeRate);
}

void RestoreArenaCinemaBar(bool& active, float& baseHeight) {
    if (!active) {
        return;
    }
    if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
        params->cinemaBarHeight = baseHeight;
    }
    active = false;
    baseHeight = 0.0f;
}

// プレイヤーの操作を戻し、共通カメラ管理へ演出の終了を通知する。
void EndArenaFixedCamera(Player* player, bool hadControl) {
    if (player && player->IsCinematicLocked()) {
        player->EndCinematicLock(hadControl);
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
    }

    CameraManager::GetInstance()->EndMainCameraPresentation();
}

} // namespace

// ボス登場・撃破報酬演出。ロック開始と復帰処理を対で管理し、進行フラグの更新順を維持する。
bool GamePlayScene::StartArenaBossIntro(engine::Object3D* bossObject, engine::Object3D* gateObject,
                                        const std::string& cinematicPath) {
    if (arenaBossIntroActive_ || isGoal_ || !player_ || !bossObject || !gateObject) {
        return false;
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        return false;
    }

    // 編集データと参照先を確かめてから操作をロックする。欠損時は通常の戦闘進行を続ける。
    // 従来のデータは保存し、標準登場だけ新しいTimelineへ切り替えます。
    const bool portalIntro = cinematicPath == "Resources/json/scenario/stage3_boss_intro.json" ||
                             cinematicPath == "Resources/json/scenario/stage3_boss_portal_intro.json";
    const std::string path = portalIntro ? "Resources/json/scenario/stage3_boss_portal_intro.json" : cinematicPath;
    if (path.empty() || !arenaBossIntroSequence_.Load(path)) {
        DebugConsole::GetInstance()->AddLog("Stage 3 boss intro timeline missing; cinematic skipped.");
        return false;
    }

    arenaBossIntroPlayer_.Initialize(SceneManager::GetInstance());
    arenaBossIntroPlayer_.SetSequence(&arenaBossIntroSequence_);
    const auto bindingIssues = arenaBossIntroPlayer_.ValidateBindings();
    if (!bindingIssues.empty()) {
        DebugConsole::GetInstance()->AddLog(bindingIssues.front());
        return false;
    }

    arenaBossIntroReturnStart_ = -1.0f;
    for (const auto& shot : arenaBossIntroSequence_.cameraShots) {
        if (!shot.enabled || shot.muted) {
            continue;
        }

        arenaBossIntroReturnStart_ = (std::max)(arenaBossIntroReturnStart_, shot.startTime);
    }

    if (arenaBossIntroReturnStart_ < 0.0f) {
        DebugConsole::GetInstance()->AddLog("Stage 3 boss intro has no camera shots; cinematic skipped.");
        return false;
    }

    const auto bossTrack = std::find_if(arenaBossIntroSequence_.transformTracks.begin(),
                                        arenaBossIntroSequence_.transformTracks.end(), [this, bossObject](const auto& track) {
                                            return track.enabled && !track.muted && arenaBossIntroPlayer_.ResolveBinding(track.binding) == bossObject;
                                        });
    if (bossTrack == arenaBossIntroSequence_.transformTracks.end() || !bossTrack->relative ||
        bossTrack->keys.empty() || bossTrack->startTime < 0.0f) {
        DebugConsole::GetInstance()->AddLog("Stage 3 boss intro pose track missing; cinematic skipped.");
        return false;
    }

    float messageInTime = -1.0f;
    float messageOutTime = -1.0f;
    float barOpenEnd = -1.0f;
    bool hasBossActivation = false;
    for (const auto& signal : arenaBossIntroSequence_.signals) {
        if (!signal.enabled) {
            continue;
        }
        if (signal.signal == "boss_intro_message_in") {
            messageInTime = signal.time;
        } else if (signal.signal == "boss_intro_message_out") {
            messageOutTime = signal.time;
        } else if (signal.signal == "boss_intro_bar_open_end") {
            barOpenEnd = signal.time;
        } else if (signal.signal == "arena.activate_boss") {
            auto* encounter = dynamic_cast<GimmickArenaEncounter*>(arenaBossIntroPlayer_.ResolveBinding(signal.binding));
            hasBossActivation = hasBossActivation ||
                                (encounter && encounter->GetTargetId() == bossObject->GetEventId());
        }
    }
    if (messageInTime < 0.0f || messageOutTime <= messageInTime || barOpenEnd <= 0.0f || !hasBossActivation) {
        DebugConsole::GetInstance()->AddLog("Stage 3 boss intro timing markers missing; cinematic skipped.");
        return false;
    }

    arenaBossIntroMessageInTime_ = messageInTime;
    arenaBossIntroMessageOutTime_ = messageOutTime;
    arenaBossIntroBarOpenEnd_ = barOpenEnd;

    arenaBossIntroGateTimelineControlled_ = false;
    for (const auto& track : arenaBossIntroSequence_.transformTracks) {
        if (!track.enabled || track.muted || arenaBossIntroPlayer_.ResolveBinding(track.binding) != gateObject) {
            continue;
        }

        if (!track.anchorRelative || !dynamic_cast<GimmickBossGate*>(gateObject)) {
            DebugConsole::GetInstance()->AddLog("Stage 3 boss gate timeline binding invalid; cinematic skipped.");
            return false;
        }
        arenaBossIntroGateTimelineControlled_ = true;
        break;
    }

    arenaBossIntroDuration_ = arenaBossIntroSequence_.GetAuthoredDuration();

    // 検証を終えてから入力を止める。開始できない場合はシーン状態を変更しない。
    if (!BeginArenaFixedCamera(*player_, true, arenaBossIntroHadPlayerControl_)) {
        return false;
    }

    // 演出開始前の操作権とカメラ値を保存し、途中終了でも Finish で戻せるようにします。
    arenaBossIntroActive_ = true;
    arenaBossIntroTimer_ = 0.0f;
    arenaBossIntroBoss_ = bossObject;
    if (auto* king = dynamic_cast<EnemyFalseKingSlime*>(bossObject)) {
        king->SetIntroPresentationActive(true, bossTrack->keys.back().time);
        king->EnableIntroPortal(portalIntro);
        auto cueTime = [this](const char* name, float fallback) {
            for (const auto& cue : arenaBossIntroSequence_.signals)
                if (cue.enabled && cue.signal == name && std::isfinite(cue.time))
                    return cue.time;
            return fallback;
        };
        king->ConfigureIntroTiming(bossTrack->startTime,
            cueTime("boss_intro_landing", 3.45f), cueTime("boss_intro_pose", messageInTime),
            cueTime("boss_intro_gate_close", 4.3f), cueTime("boss_intro_aura_out", 6.0f));
    }
    arenaBossIntroGate_ = gateObject;

    const engine::Vector3 playerPosition = player_->GetWorldPosition();
    const engine::Vector3 bossPosition = bossObject->GetWorldPosition();
    const engine::Vector3 bossToPlayer = NormalizePlanarDirection(playerPosition - bossPosition, {0.0f, 0.0f, 1.0f});

    // 閉じた格子より内側へ収まる戦闘用構図を別に保持し、入口直前の
    // カメラ衝突状態へ戻してしまわないようにします。
    arenaBossIntroRestoreTarget_ = playerPosition + engine::Vector3{0.0f, kArenaBossGameplayCameraHeight, 0.0f};
    arenaBossIntroRestoreEye_ =
        arenaBossIntroRestoreTarget_ + bossToPlayer * kArenaBossGameplayCameraDistance + engine::Vector3{0.0f, 3.7f, 0.0f};

    // 動画中はプレイヤーの速度とカメラ追従を止め、各ショットの固定視点を使います。
    CaptureArenaCinemaBar(arenaBossIntroCinemaBarOverrideActive_, arenaBossIntroCinemaBarBaseHeight_);

    if (arenaBossIntroGateTimelineControlled_) {
        static_cast<GimmickBossGate*>(gateObject)->SetIntroTimelineControl(true);
    }

    arenaBossIntroPlayer_.SetSignalCallback([](engine::Object3D* target, const CinematicSignalMarker& signal,
                                               bool isPreview) {
        if (isPreview || signal.signal != "arena.activate_boss") {
            return;
        }
        if (auto* encounter = dynamic_cast<GimmickArenaEncounter*>(target)) {
            encounter->ActivateBossFromTimeline();
        }
    });
    arenaBossIntroPlayer_.Play(false);

    DebugConsole::GetInstance()->AddLog("Stage 3: False King boss introduction started.");
    return true;
}

void GamePlayScene::UpdateArenaBossIntro(float deltaTime) {
    // 演出対象がシーンから消えた場合は途中で閉じ、操作権を返す。
    if (!arenaBossIntroActive_ || !player_ || !arenaBossIntroBoss_ || !arenaBossIntroGate_) {
        FinishArenaBossIntro();
        return;
    }

    arenaBossIntroPlayer_.Update((std::max)(deltaTime, 0.0f));
    arenaBossIntroTimer_ = arenaBossIntroPlayer_.GetCurrentTime();
    if(auto* king=dynamic_cast<EnemyFalseKingSlime*>(arenaBossIntroBoss_))
        king->UpdateIntroPortal(arenaBossIntroTimer_,(std::max)(deltaTime,0.0f));
    player_->SetVelocity({0.0f, 0.0f, 0.0f});

    // シネマバーは冒頭で開き、戦闘画面に戻す終盤で閉じます。
    if (arenaBossIntroCinemaBarOverrideActive_) {
        UpdateArenaCinemaBar(arenaBossIntroTimer_, arenaBossIntroBarOpenEnd_, arenaBossIntroReturnStart_,
                             arenaBossIntroDuration_, kArenaBossIntroCinemaBarHeight,
                             arenaBossIntroCinemaBarBaseHeight_);
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    if (!camera) {
        FinishArenaBossIntro();
        return;
    }

    // カメラとゲートの動きは Timeline 側で評価する。
    CameraManager::GetInstance()->SetActiveCamera(camera);

    if (!arenaBossIntroPlayer_.IsPlaying()) {
        FinishArenaBossIntro();
    }
}

void GamePlayScene::FinishArenaBossIntro() {
    // 導入演出中に変更したカメラ・操作状態を通常戦闘へ戻す。
    if (!arenaBossIntroActive_) {
        return;
    }

    arenaBossIntroActive_ = false;
    arenaBossIntroPlayer_.Stop(true);
    arenaBossIntroPlayer_.SetSignalCallback({});

    if (arenaBossIntroGateTimelineControlled_) {
        if (auto* gate = dynamic_cast<GimmickBossGate*>(arenaBossIntroGate_)) {
            gate->SetIntroTimelineControl(false);
        }
    }

    arenaBossIntroGateTimelineControlled_ = false;
    if (auto* king = dynamic_cast<EnemyFalseKingSlime*>(arenaBossIntroBoss_))
        king->SetIntroPresentationActive(false);
    arenaBossIntroTimer_ = 0.0f;
    arenaBossIntroBoss_ = nullptr;
    arenaBossIntroGate_ = nullptr;

    RestoreArenaCinemaBar(arenaBossIntroCinemaBarOverrideActive_, arenaBossIntroCinemaBarBaseHeight_);
    CameraManager::GetInstance()->PrepareMainCameraReturn(player_, arenaBossIntroRestoreEye_,
                                                          arenaBossIntroRestoreTarget_);
    CameraManager::GetInstance()->SnapMainCameraReturn(kArenaBossGameplayCameraDistance,
                                                       kArenaBossGameplayCameraHeight, kArenaBossGameplayCameraPitch);
    EndArenaFixedCamera(player_, arenaBossIntroHadPlayerControl_);
}

void GamePlayScene::StartArenaBossDefeatReward(engine::Object3D* rewardObject) {
    if (crownDefeat_.active || crownDefeat_.completed || arenaBossRewardActive_ || isGoal_ || !player_ || !rewardObject) {
        return;
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera || !arenaBossRewardSequence_.Load(kArenaBossRewardSequencePath)) {
        DebugConsole::GetInstance()->AddLog("Stage 3 boss reward timeline missing; cinematic skipped.");
        return;
    }

    arenaBossRewardPlayer_.Initialize(SceneManager::GetInstance());
    arenaBossRewardPlayer_.SetSequence(&arenaBossRewardSequence_);
    const auto bindingIssues = arenaBossRewardPlayer_.ValidateBindings();
    if (!bindingIssues.empty()) {
        DebugConsole::GetInstance()->AddLog(bindingIssues.front());
        return;
    }

    // カメラ復帰の合図は編集データから取得し、演出開始前に検証する。
    arenaBossRewardReturnStart_ = -1.0f;
    for (const auto& signal : arenaBossRewardSequence_.signals) {
        if (signal.enabled && signal.signal == "camera.resume_gameplay") {
            arenaBossRewardReturnStart_ = signal.time;
            break;
        }
    }
    arenaBossRewardDuration_ = arenaBossRewardPlayer_.GetDuration();
    if (arenaBossRewardReturnStart_ < 0.0f || arenaBossRewardReturnStart_ >= arenaBossRewardDuration_ ||
        arenaBossRewardSequence_.cameraShots.empty()) {
        DebugConsole::GetInstance()->AddLog("Stage 3 boss reward camera markers missing; cinematic skipped.");
        return;
    }

    if (!BeginArenaFixedCamera(*player_, false, arenaBossRewardHadPlayerControl_)) {
        return;
    }

    // 通常視点への復帰は共通カメラAPIに任せ、シーンは演出開始イベントだけを受け持つ。
    arenaBossRewardActive_ = true;
    arenaBossRewardReturnPrepared_ = false;
    arenaBossRewardObject_ = rewardObject;
    arenaBossRewardGameplayEye_ = camera->GetEye();
    arenaBossRewardGameplayTarget_ = camera->GetTargetPoint();
    CaptureArenaCinemaBar(arenaBossRewardCinemaBarOverrideActive_, arenaBossRewardCinemaBarBaseHeight_);

    arenaBossRewardPlayer_.SetSignalCallback([this](engine::Object3D*, const CinematicSignalMarker& signal,
                                                    bool isPreview) {
        if (!isPreview && signal.signal == "camera.resume_gameplay") {
            CameraManager::GetInstance()->PrepareMainCameraReturn(player_, arenaBossRewardGameplayEye_,
                                                                  arenaBossRewardGameplayTarget_);
            arenaBossRewardReturnPrepared_ = true;
        }
    });
    arenaBossRewardPlayer_.Play(false);

    DebugConsole::GetInstance()->AddLog("Stage 3: boss crown reward cinematic started.");
}

void GamePlayScene::UpdateArenaBossDefeatReward(float deltaTime) {
    // 報酬対象が消えた場合も終了処理を通し、演出状態を残さない。
    if (!arenaBossRewardActive_ || !player_ || !arenaBossRewardObject_) {
        FinishArenaBossDefeatReward();
        return;
    }

    arenaBossRewardPlayer_.Update((std::max)(0.0f, deltaTime));
    const float time = arenaBossRewardPlayer_.GetCurrentTime();
    player_->SetVelocity({0.0f, 0.0f, 0.0f});

    // 報酬への寄りと戦闘画面への復帰に合わせてシネマバーを開閉する。
    if (arenaBossRewardCinemaBarOverrideActive_) {
        UpdateArenaCinemaBar(time, 0.20f, arenaBossRewardReturnStart_, arenaBossRewardDuration_,
                             kArenaBossRewardCinemaBarHeight, arenaBossRewardCinemaBarBaseHeight_);
    }

    if (!arenaBossRewardPlayer_.IsPlaying()) {
        FinishArenaBossDefeatReward();
    }
}

void GamePlayScene::FinishArenaBossDefeatReward() {
    // 終了時は途中で中断された場合も通常視点と操作権を返す。
    if (!arenaBossRewardActive_) {
        return;
    }

    arenaBossRewardActive_ = false;
    arenaBossRewardPlayer_.Stop(true);
    arenaBossRewardPlayer_.SetSignalCallback({});
    arenaBossRewardObject_ = nullptr;

    if (!arenaBossRewardReturnPrepared_) {
        CameraManager::GetInstance()->PrepareMainCameraReturn(player_, arenaBossRewardGameplayEye_,
                                                              arenaBossRewardGameplayTarget_);
    }
    arenaBossRewardReturnPrepared_ = false;
    RestoreArenaCinemaBar(arenaBossRewardCinemaBarOverrideActive_, arenaBossRewardCinemaBarBaseHeight_);
    EndArenaFixedCamera(player_, arenaBossRewardHadPlayerControl_);
}

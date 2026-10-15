#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "PostEffect.h"
#include "engine/utility/math/AnimationInterpolation.h"
#include "game/actor/gimmick/trigger/GimmickBossGate.h"
#include <algorithm>
#include <cmath>
#include <numbers>

#include "game/scene/gameplay/GamePlayScene.PresentationMath.h"
using namespace GamePlayPresentationMath;

namespace {
constexpr float kArenaBossIntroGateShotEnd = 0.74f;
constexpr float kArenaBossIntroGateHoldEnd = 1.05f;
constexpr float kArenaBossIntroBossFocusEnd = 1.55f;
constexpr float kArenaBossIntroBossHoldEnd = 5.45f;
constexpr float kArenaBossIntroDuration = 6.45f;
constexpr float kArenaBossIntroCinemaBarHeight = 0.115f;
constexpr float kArenaBossGameplayCameraDistance = 11.5f;
constexpr float kArenaBossGameplayCameraHeight = 3.8f;
constexpr float kArenaBossGameplayCameraPitch = 0.31415927f;
constexpr float kArenaBossRewardFocusInEnd = 0.32f;
constexpr float kArenaBossRewardTrackEnd = 1.82f;
constexpr float kArenaBossRewardDuration = 2.78f;
constexpr float kArenaBossRewardCinemaBarHeight = 0.115f;

} // namespace

// ボス登場・撃破報酬演出。ロック開始と復帰処理を対で管理し、進行フラグの更新順を維持する。
void GamePlayScene::StartArenaBossIntro(engine::Object3d* bossObject, engine::Object3d* gateObject, float bossRevealDelay) {
    if (arenaBossIntroActive_ || isGoal_ || !player_ || !bossObject || !gateObject) {
        return;
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        return;
    }

    // 演出開始前の操作権とカメラ値を保存し、途中終了でも Finish で戻せるようにします。
    arenaBossIntroActive_ = true;
    arenaBossIntroTimer_ = 0.0f;
    arenaBossIntroRevealDelay_ = std::clamp(bossRevealDelay, 0.20f, 1.40f);
    arenaBossIntroBoss_ = bossObject;
    if (auto* king = dynamic_cast<EnemyFalseKingSlime*>(bossObject))
        king->SetIntroPresentationActive(true);
    arenaBossIntroGate_ = gateObject;
    arenaBossIntroHadPlayerControl_ = player_->IsControlActive();
    arenaBossIntroGameplayEye_ = camera->GetEye();
    arenaBossIntroGameplayTarget_ = camera->GetTargetPoint();
    arenaBossIntroBaseFov_ = camera->GetFovY();

    const engine::Vector3 playerPosition = player_->GetWorldPosition();
    const engine::Vector3 bossPosition = bossObject->GetWorldPosition();
    engine::Vector3 gatePosition = gateObject->GetWorldPosition();
    if (auto* bossGate = dynamic_cast<GimmickBossGate*>(gateObject)) {
        gatePosition = bossGate->GetClosedPosition();
    }
    const engine::Vector3 bossToPlayer = NormalizePlanarDirection(playerPosition - bossPosition, {0.0f, 0.0f, 1.0f});
    const engine::Vector3 right = {bossToPlayer.z, 0.0f, -bossToPlayer.x};

    // 閉じた格子より内側へ収まる戦闘用構図を別に保持し、入口直前の
    // カメラ衝突状態へ戻してしまわないようにします。
    arenaBossIntroRestoreTarget_ = playerPosition + engine::Vector3{0.0f, kArenaBossGameplayCameraHeight, 0.0f};
    arenaBossIntroRestoreEye_ =
        arenaBossIntroRestoreTarget_ + bossToPlayer * kArenaBossGameplayCameraDistance + engine::Vector3{0.0f, 3.7f, 0.0f};

    // 最初に背後の格子ゲートを見せ、封鎖されたことを一目で理解できる構図にします。
    arenaBossIntroGateTarget_ = gatePosition + engine::Vector3{0.0f, 3.85f, 0.0f};
    arenaBossIntroGateEye_ = gatePosition - bossToPlayer * 22.0f + right * 2.6f + engine::Vector3{0.0f, 5.35f, 0.0f};

    // 正面全身を画面中央へ収め、登場時の落下、潰れ、跳ね返りを見せます。
    arenaBossIntroBossTarget_ = bossPosition + engine::Vector3{0.0f, 4.70f, 0.0f};
    arenaBossIntroBossEye_ = bossPosition + bossToPlayer * 31.0f + right * 2.6f + engine::Vector3{0.0f, 5.8f, 0.0f};

    // 動画中はプレイヤーの速度とカメラ追従を止め、各ショットの固定視点を使います。
    player_->BeginCinematicLock(true);
    player_->SetVelocity({0.0f, 0.0f, 0.0f});
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
    // 演出対象がシーンから消えた場合は途中で閉じ、操作権を返す。
    if (!arenaBossIntroActive_ || !player_ || !arenaBossIntroBoss_ || !arenaBossIntroGate_) {
        FinishArenaBossIntro();
        return;
    }

    arenaBossIntroTimer_ += std::max(deltaTime, 0.0f);
    player_->SetVelocity({0.0f, 0.0f, 0.0f});

    // シネマバーは冒頭で開き、戦闘画面に戻す終盤で閉じます。
    if (arenaBossIntroCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            const float openRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(arenaBossIntroTimer_, 0.0f, 0.24f),
                AnimationInterpolation::EasingType::SmootherStep);
            const float closeRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(arenaBossIntroTimer_, kArenaBossIntroBossHoldEnd,
                                                    kArenaBossIntroDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            const float blend = openRate * (1.0f - closeRate);
            params->cinemaBarHeight = arenaBossIntroCinemaBarBaseHeight_ +
                                      (std::max(arenaBossIntroCinemaBarBaseHeight_, kArenaBossIntroCinemaBarHeight) -
                                       arenaBossIntroCinemaBarBaseHeight_) *
                                          blend;
        }
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        FinishArenaBossIntro();
        return;
    }

    // 落下・跳ね返り中のボスの動きに合わせて注視点だけを少し追従させます。
    const engine::Vector3 currentBossPosition = arenaBossIntroBoss_->GetWorldPosition();
    const float authoredBossY = arenaBossIntroBossTarget_.y - 4.70f;
    const float bossRise = std::clamp(currentBossPosition.y - authoredBossY, -0.4f, 3.6f);
    const engine::Vector3 trackedBossTarget = arenaBossIntroBossTarget_ + engine::Vector3{0.0f, bossRise * 0.35f, 0.0f};

    engine::Vector3 eye = arenaBossIntroGameplayEye_;
    engine::Vector3 target = arenaBossIntroGameplayTarget_;
    float fov = arenaBossIntroBaseFov_;
    // ゲート寄り → ゲート保持 → ボスへ移動 → ボス保持 → 戦闘構図への復帰を時刻で選びます。
    if (arenaBossIntroTimer_ < kArenaBossIntroGateShotEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(arenaBossIntroTimer_, 0.0f, kArenaBossIntroGateShotEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        const engine::Vector3 gateRetreatDirection =
            NormalizePlanarDirection(arenaBossIntroGateEye_ - arenaBossIntroGateTarget_, {0.0f, 0.0f, -1.0f});
        const engine::Vector3 gateDollyStartEye = arenaBossIntroGateEye_ + gateRetreatDirection * 2.8f;
        eye = AnimationInterpolation::Lerp(gateDollyStartEye, arenaBossIntroGateEye_, rate);
        target = arenaBossIntroGateTarget_;
        fov = AnimationInterpolation::Lerp(0.55f, 0.52f, rate);
    } else if (arenaBossIntroTimer_ < kArenaBossIntroGateHoldEnd) {
        eye = arenaBossIntroGateEye_;
        target = arenaBossIntroGateTarget_;
        fov = 0.52f;
    } else if (arenaBossIntroTimer_ < kArenaBossIntroBossFocusEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(arenaBossIntroTimer_, kArenaBossIntroGateHoldEnd,
                                                kArenaBossIntroBossFocusEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(arenaBossIntroGateEye_, arenaBossIntroBossEye_, rate);
        target = AnimationInterpolation::Lerp(arenaBossIntroGateTarget_, trackedBossTarget, rate);
        fov = AnimationInterpolation::Lerp(0.52f, 0.51f, rate);
    } else if (arenaBossIntroTimer_ < kArenaBossIntroBossHoldEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(arenaBossIntroTimer_, kArenaBossIntroBossFocusEnd,
                                                kArenaBossIntroBossHoldEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        const engine::Vector3 heroEye = arenaBossIntroBossEye_ + engine::Vector3{-2.4f * rate, -0.8f * rate, -2.0f * rate};
        eye = heroEye;
        target = trackedBossTarget;
        fov = AnimationInterpolation::Lerp(0.51f, 0.48f, rate);
    } else {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(arenaBossIntroTimer_, kArenaBossIntroBossHoldEnd,
                                                kArenaBossIntroDuration),
            AnimationInterpolation::EasingType::SmootherStep);
        const engine::Vector3 heroEye = arenaBossIntroBossEye_ + engine::Vector3{-2.4f, -0.8f, -2.0f};
        eye = AnimationInterpolation::Lerp(heroEye, arenaBossIntroRestoreEye_, rate);
        target = AnimationInterpolation::Lerp(trackedBossTarget, arenaBossIntroRestoreTarget_, rate);
        fov = AnimationInterpolation::Lerp(0.48f, arenaBossIntroBaseFov_, rate);
    }

    // FixedPoint の視点・向き・FOV を同じフレームへ適用し、他の追従処理に奪われないようにします。
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(eye);
    camera->SetTarget(target);
    const engine::Vector3 rotation = MakeLookAtEuler(eye, target);
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
    // 導入演出中に変更したカメラ・操作状態を通常戦闘へ戻す。
    if (!arenaBossIntroActive_) {
        return;
    }

    arenaBossIntroActive_ = false;
    if (auto* king = dynamic_cast<EnemyFalseKingSlime*>(arenaBossIntroBoss_))
        king->SetIntroPresentationActive(false);
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
        camera->SnapToThirdPerson(kArenaBossGameplayCameraDistance, kArenaBossGameplayCameraHeight,
                                  kArenaBossGameplayCameraPitch);
        camera->SetInputEnabled(true);
    }
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
}

void GamePlayScene::StartArenaBossDefeatReward(engine::Object3d* rewardObject) {
    if (crownDefeat_.active || crownDefeat_.completed || arenaBossRewardActive_ || isGoal_ || !player_ || !rewardObject) {
        return;
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        return;
    }

    // 報酬へ注目する前のカメラと操作権を保存し、演出の終了時に必ず復元します。
    arenaBossRewardActive_ = true;
    arenaBossRewardTimer_ = 0.0f;
    arenaBossRewardObject_ = rewardObject;
    arenaBossRewardHadPlayerControl_ = player_->IsControlActive();
    arenaBossRewardGameplayEye_ = camera->GetEye();
    arenaBossRewardGameplayTarget_ = camera->GetTargetPoint();
    arenaBossRewardBaseFov_ = camera->GetFovY();

    // EventReceiverの起動直後はWorld行列更新前なので、親を持たない報酬王冠の編集値を直接使います。
    const engine::Vector3 crownPosition = rewardObject->GetTranslate();
    const engine::Vector3 playerPosition = player_->GetWorldPosition();
    const engine::Vector3 crownToPlayer = NormalizePlanarDirection(playerPosition - crownPosition, {0.0f, 0.0f, 1.0f});
    const engine::Vector3 right = {crownToPlayer.z, 0.0f, -crownToPlayer.x};
    arenaBossRewardFocusTarget_ = crownPosition + engine::Vector3{0.0f, 1.15f, 0.0f};
    arenaBossRewardFocusEye_ = crownPosition + crownToPlayer * 7.4f + right * 2.9f + engine::Vector3{0.0f, 3.4f, 0.0f};

    // 王冠の受け取り中はプレイヤーの移動を止め、カメラを固定ショットへ切り替えます。
    player_->BeginCinematicLock();
    player_->SetVelocity({0.0f, 0.0f, 0.0f});
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
    // 報酬対象が消えた場合も終了処理を通し、演出状態を残さない。
    if (!arenaBossRewardActive_ || !player_ || !arenaBossRewardObject_) {
        FinishArenaBossDefeatReward();
        return;
    }

    arenaBossRewardTimer_ += (std::max)(0.0f, deltaTime);
    player_->SetVelocity({0.0f, 0.0f, 0.0f});

    // 報酬への寄りと戦闘画面への復帰に合わせてシネマバーを開閉します。
    if (arenaBossRewardCinemaBarOverrideActive_) {
        if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
            const float openRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(arenaBossRewardTimer_, 0.0f, 0.20f),
                AnimationInterpolation::EasingType::SmootherStep);
            const float closeRate = AnimationInterpolation::ApplyEasing(
                AnimationInterpolation::SegmentRate(arenaBossRewardTimer_, kArenaBossRewardTrackEnd,
                                                    kArenaBossRewardDuration),
                AnimationInterpolation::EasingType::SmootherStep);
            const float blend = openRate * (1.0f - closeRate);
            params->cinemaBarHeight =
                arenaBossRewardCinemaBarBaseHeight_ +
                ((std::max)(arenaBossRewardCinemaBarBaseHeight_, kArenaBossRewardCinemaBarHeight) -
                 arenaBossRewardCinemaBarBaseHeight_) *
                    blend;
        }
    }

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) {
        FinishArenaBossDefeatReward();
        return;
    }

    const engine::Vector3 crownPosition = arenaBossRewardObject_->GetTranslate();
    engine::Vector3 eye = arenaBossRewardGameplayEye_;
    engine::Vector3 target = arenaBossRewardGameplayTarget_;
    float fov = arenaBossRewardBaseFov_;
    // 報酬へ寄る → 移動中の王冠を追う → 元の視点へ戻る、の順に構図を補間します。
    if (arenaBossRewardTimer_ < kArenaBossRewardFocusInEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(arenaBossRewardTimer_, 0.0f, kArenaBossRewardFocusInEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        eye = AnimationInterpolation::Lerp(arenaBossRewardGameplayEye_, arenaBossRewardFocusEye_, rate);
        target = AnimationInterpolation::Lerp(arenaBossRewardGameplayTarget_, arenaBossRewardFocusTarget_, rate);
        fov = AnimationInterpolation::Lerp(arenaBossRewardBaseFov_, 0.42f, rate);
    } else if (arenaBossRewardTimer_ < kArenaBossRewardTrackEnd) {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(arenaBossRewardTimer_, kArenaBossRewardFocusInEnd,
                                                kArenaBossRewardTrackEnd),
            AnimationInterpolation::EasingType::SmootherStep);
        const engine::Vector3 trackedTarget = crownPosition + engine::Vector3{0.0f, 1.0f, 0.0f};
        const engine::Vector3 trackedEye = crownPosition + engine::Vector3{6.2f, 3.3f, 7.4f};
        eye = AnimationInterpolation::Lerp(arenaBossRewardFocusEye_, trackedEye, rate);
        target = AnimationInterpolation::Lerp(arenaBossRewardFocusTarget_, trackedTarget, rate);
        fov = AnimationInterpolation::Lerp(0.42f, 0.47f, rate);
    } else {
        const float rate = AnimationInterpolation::ApplyEasing(
            AnimationInterpolation::SegmentRate(arenaBossRewardTimer_, kArenaBossRewardTrackEnd,
                                                kArenaBossRewardDuration),
            AnimationInterpolation::EasingType::SmootherStep);
        const engine::Vector3 settleTarget = crownPosition + engine::Vector3{0.0f, 1.0f, 0.0f};
        const engine::Vector3 settleEye = crownPosition + engine::Vector3{6.2f, 3.3f, 7.4f};
        eye = AnimationInterpolation::Lerp(settleEye, arenaBossRewardGameplayEye_, rate);
        target = AnimationInterpolation::Lerp(settleTarget, arenaBossRewardGameplayTarget_, rate);
        fov = AnimationInterpolation::Lerp(0.47f, arenaBossRewardBaseFov_, rate);
    }

    // 導入演出と同じ FixedPoint 経路を使い、補間した視点を毎フレーム反映します。
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(eye);
    camera->SetTarget(target);
    const engine::Vector3 rotation = MakeLookAtEuler(eye, target);
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
    // 報酬表示後の操作・カメラ状態を通常のシーン更新へ返す。
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

#define NOMINMAX
#include "game/actor/player/state/PlayerState.h"
#include "game/actor/player/Player.h"
#include "DebugConsole.h"
#include "engine/graphics/postprocess/Fade.h"
#include "engine/graphics/postprocess/PostEffect.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include "game/scene/gameplay/GamePlayScene.h"
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cmath>
#include <memory>

// 死亡と落下復帰の演出状態をまとめています。

namespace {
engine::Vector2 CalculatePlayerIrisCenter(Player* player) {
    // プレイヤーが画面内にいる時だけ投影位置を使い、画面外なら中央から閉じる。
    engine::Vector2 irisCenter = {0.5f, 0.5f};
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (!player || !cam) {
        return irisCenter;
    }

    engine::Vector3 worldPos = player->GetWorldPosition();
    worldPos.y += 1.0f;
    engine::Vector3 ndc = engine::Math::Transform(worldPos, cam->GetViewProjectionMatrix());
    const bool isOnScreen = std::isfinite(ndc.x) && std::isfinite(ndc.y) && std::isfinite(ndc.z) && ndc.z >= 0.0f &&
                            std::abs(ndc.x) <= 1.15f && std::abs(ndc.y) <= 1.15f;
    if (isOnScreen) {
        irisCenter = {std::clamp((ndc.x + 1.0f) * 0.5f, 0.18f, 0.82f), std::clamp((1.0f - ndc.y) * 0.5f, 0.16f, 0.78f)};
    }
    return irisCenter;
}

GamePlayScene* GetCurrentGamePlayScene() {
    // 死亡演出はゲームシーン内に限って通知し、プレビュー等では省略する。
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager) {
        return nullptr;
    }
    return dynamic_cast<GamePlayScene*>(sceneManager->GetCurrentScene());
}

// 現在のGamePlaySceneへ残機減少前後の値を渡し、死亡演出を開始します。
void StartLifeLostPresentationOnScene() {
    // 減少後の残機に対し、演出には減少前後の値を渡す。
    if (GamePlayScene* scene = GetCurrentGamePlayScene()) {
        const int afterLives = GameDataManager::GetInstance()->GetLives();
        scene->StartLifeLostPresentation(afterLives + 1, afterLives);
    }
}
} // namespace

// ========================================================
// 死亡状態 (Dead)
// ========================================================
void PlayerStateDead::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Dead");
    timer_ = 0.0f;
    sceneChangeRequested_ = false;
    lifePresentationStarted_ = false;
    finalDeath_ = GameDataManager::GetInstance()->GetLives() <= 1;
    // プレビューの死亡演出は隔離し、実戦のカメラ・暗転・シーン遷移を操作しません。
    if (player && player->IsEditorInternal()) {
        deathAnimation_.Start(player, false);
        player->SetIsControlActive(false);
        return;
    }
    Fade::GetInstance()->Stop();
    if (player) {
        player->SetDamageInvincible(false);
        deathAnimation_.Start(player, finalDeath_);
        if (engine::Object3d* hookMarker = player->GetHookMarker()) {
            hookMarker->SetIsVisible(false);
        }
        Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
        if (cam && !(GetCurrentGamePlayScene() && GetCurrentGamePlayScene()->IsSealBarrageMovieActive())) {
            cam->SetFreezeEye(true);
            if (finalDeath_) {
                const engine::Vector3 focus = player->GetWorldPosition() + engine::Vector3{0.0f, 0.58f, 0.0f};
                engine::Vector3 focusEye = engine::Math::Lerp(cam->GetEye(), focus, 0.16f);
                focusEye.y += 0.10f;

                Camera::CameraOverrideParams cameraOverride;
                cameraOverride.duration = 0.24f;
                cameraOverride.exitDuration = 0.0f;
                cameraOverride.easing = Camera::OverrideEasing::kEaseOut;
                cameraOverride.fixedEyePos = focusEye;
                cameraOverride.fixedTargetPos = focus;
                cameraOverride.trackEyeX = false;
                cameraOverride.trackEyeY = false;
                cameraOverride.trackEyeZ = false;
                cameraOverride.trackTargetX = false;
                cameraOverride.trackTargetY = false;
                cameraOverride.trackTargetZ = false;
                cam->StartOverride(cameraOverride);
                cam->StartShake(0.22f, 0.085f, 30.0f, {0.85f, 0.60f, 0.35f});
            }
        }
        irisCenter_ = CalculatePlayerIrisCenter(player);
    }
    if (player)
        player->SetIsControlActive(false);
}

void PlayerStateDead::Update(Player* player, float deltaTime) {
    if (!player || sceneChangeRequested_)
        return;

    if (player->IsEditorInternal()) {
        deathAnimation_.Update(player, deltaTime);
        return;
    }

    // 即死のHP判定は済んでいても、全域放電と槍の崩壊が終わるまで暗転を待ちます。
    if (auto* scene = GetCurrentGamePlayScene(); scene && scene->IsSealBarrageMovieActive())
        return;

    if (timer_ == 0.0f)
        if (Camera* camera = CameraManager::GetInstance()->GetActiveCamera())
            camera->SetFreezeEye(true);
    timer_ += deltaTime;
    finalDeath_ = finalDeath_ || GameDataManager::GetInstance()->GetLives() <= 0;
    deathAnimation_.Update(player, deltaTime);

    const float irisDuration = finalDeath_ ? 1.45f : 1.25f;
    if (deathAnimation_.IsReadyForFade() && !lifePresentationStarted_ &&
        Fade::GetInstance()->GetStatus() == Fade::Status::None) {
        Fade::GetInstance()->StartIrisOut(irisDuration, irisCenter_);
    }

    if (!lifePresentationStarted_ && Fade::GetInstance()->IsFinished()) {
        Fade::GetInstance()->Stop();
        lifePresentationStarted_ = true;
        StartLifeLostPresentationOnScene();
        return;
    }

    if (!lifePresentationStarted_) {
        return;
    }

    GamePlayScene* scene = GetCurrentGamePlayScene();
    if (scene && !scene->IsLifeLostPresentationFinished()) {
        return;
    }

    sceneChangeRequested_ = true;
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        if (cam->IsOverridden()) {
            cam->EndOverride(0.0f);
        }
        cam->SetFreezeEye(false);
    }

    if (GameDataManager::GetInstance()->GetLives() <= 0) {
        Fade::GetInstance()->Stop();
        SceneManager::GetInstance()->ChangeSceneWithoutLoading("GAMEOVER");
    } else {
        GameDataManager::GetInstance()->RequestRespawnIrisIn();
        SceneManager::GetInstance()->ChangeSceneWithoutLoading("GAMEPLAY", true);
    }
}

void PlayerStateDead::Exit(Player* player) {
    // シーンが継続する場合でも、カメラ固定と操作禁止を残さない。
    if (player) {
        deathAnimation_.RestoreVisual(player);
        player->SetIsControlActive(true);
    }
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        if (cam->IsOverridden()) {
            cam->EndOverride(0.0f);
        }
        cam->SetFreezeEye(false);
    }
}

// ========================================================
// 落下演出状態 (FallingOut)
// ========================================================
void PlayerStateFallingOut::Enter(Player* player) {
    // 落下直後の画面位置をアイリス中心として固定し、入力を止める。
    if (!player)
        return;
    player->SetIsControlActive(false);
    irisCenter_ = CalculatePlayerIrisCenter(player);
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        cam->SetFreezeEye(true);
    }

    phase_ = Phase::Waiting;
    waitTimer_ = 0.0f;
}

void PlayerStateFallingOut::Update(Player* player, float deltaTime) {
    if (!player)
        return;

    // 共通処理: アイリスの中心をプレイヤーに合わせ続ける (IrisOut/IrisIn中)
    if (phase_ == Phase::IrisOut || phase_ == Phase::IrisIn) {
        PostEffect::GetInstance()->GetParams()->irisCenterX = irisCenter_.x;
        PostEffect::GetInstance()->GetParams()->irisCenterY = irisCenter_.y;
    }

    // フェーズごとの処理
    switch (phase_) {
    case Phase::Waiting:
        waitTimer_ += deltaTime;
        if (waitTimer_ >= 1.55f) {
            // アイリスアウト開始
            Fade::GetInstance()->StartIrisOut(1.35f, irisCenter_);
            phase_ = Phase::IrisOut;
        }
        break;

    case Phase::IrisOut:
        if (Fade::GetInstance()->IsFinished()) {
            Fade::GetInstance()->Stop();
            StartLifeLostPresentationOnScene();
            phase_ = Phase::LifeLost;
            break;
        }
        break;

    case Phase::LifeLost:
        if (GamePlayScene* scene = GetCurrentGamePlayScene()) {
            if (!scene->IsLifeLostPresentationFinished()) {
                break;
            }
        }

        if (GameDataManager::GetInstance()->GetLives() <= 0) {
            CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
            Fade::GetInstance()->Stop();
            SceneManager::GetInstance()->ChangeSceneWithoutLoading("GAMEOVER");
            return;
        }

        player->SetTranslate(player->GetRespawnPosition());
        player->SetVelocity({0, 0, 0});

        CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
        CameraManager::GetInstance()->GetActiveCamera()->Update(deltaTime);
        CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(true);

        irisCenter_ = CalculatePlayerIrisCenter(player);
        if (GamePlayScene* scene = GetCurrentGamePlayScene()) {
            scene->HideLifeLostPresentationOverlay();
        }
        Fade::GetInstance()->StartIrisIn(1.25f, irisCenter_);
        phase_ = Phase::IrisIn;
        break;

    case Phase::IrisIn:
        if (Fade::GetInstance()->IsFinished()) {
            // 全ての演出終了
            Fade::GetInstance()->Stop();
            player->ChangeState(std::make_unique<PlayerStateIdle>());
            player->SetIsControlActive(true);
        }
        break;
    }
}

void PlayerStateFallingOut::Exit(Player* player) {
    // 復帰後や遷移時に入力とカメラの固定を必ず解除する。
    if (player) {
        player->SetIsControlActive(true);
    }
    CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
}

#define NOMINMAX
#include "PlayerState.h"
#include "Player.h"
#include "DebugConsole.h"
#include "engine/graphics/postprocess/Fade.h"
#include "engine/graphics/postprocess/PostEffect.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include "GamePlayScene.h"
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cmath>
#include <memory>

// 死亡と落下復帰の演出状態をまとめています。

namespace {
Vector2 CalculatePlayerIrisCenter(Player* player) {
    Vector2 irisCenter = { 0.5f, 0.5f };
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (!player || !cam) {
        return irisCenter;
    }

    Vector3 worldPos = player->GetWorldPosition();
    worldPos.y += 1.0f;
    Vector3 ndc = Math::Transform(worldPos, cam->GetViewProjectionMatrix());
    const bool isOnScreen =
        std::isfinite(ndc.x) &&
        std::isfinite(ndc.y) &&
        std::isfinite(ndc.z) &&
        ndc.z >= 0.0f &&
        std::abs(ndc.x) <= 1.15f &&
        std::abs(ndc.y) <= 1.15f;
    if (isOnScreen) {
        irisCenter = {
            std::clamp((ndc.x + 1.0f) * 0.5f, 0.18f, 0.82f),
            std::clamp((1.0f - ndc.y) * 0.5f, 0.16f, 0.78f)
        };
    }
    return irisCenter;
}

GamePlayScene* GetCurrentGamePlayScene() {
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!sceneManager) {
        return nullptr;
    }
    return dynamic_cast<GamePlayScene*>(sceneManager->GetCurrentScene());
}

void StartLifeLostPresentationOnScene() {
    if (GamePlayScene* scene = GetCurrentGamePlayScene()) {
        const int afterLives = GameDataManager::GetInstance()->GetLives();
        scene->StartLifeLostPresentation(afterLives + 1, afterLives);
    }
}
}



// ========================================================
// 死亡状態 (Dead)
// ========================================================
void PlayerStateDead::Enter(Player* player) {
    DebugConsole::GetInstance()->AddLog("Enter: Dead");
    timer_ = 0.0f;
    sceneChangeRequested_ = false;
    lifePresentationStarted_ = false;
    finalDeath_ = GameDataManager::GetInstance()->GetLives() <= 1;
    Fade::GetInstance()->Stop();
    if (player) {
        player->SetDamageInvincible(false);
        deathAnimation_.Start(player, finalDeath_);
        if (Object3d* hookMarker = player->GetHookMarker()) {
            hookMarker->SetIsVisible(false);
        }
        Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
        if (cam) {
            cam->SetFreezeEye(true);
        }
        irisCenter_ = CalculatePlayerIrisCenter(player);
    }
    if (player) player->SetIsControlActive(false);
}

void PlayerStateDead::Update(Player* player, float deltaTime) {
    if (!player || sceneChangeRequested_) return;

    timer_ += deltaTime;
    finalDeath_ = finalDeath_ || GameDataManager::GetInstance()->GetLives() <= 0;
    deathAnimation_.Update(player, deltaTime);

    const float irisDuration = finalDeath_ ? 1.45f : 1.25f;
    if (deathAnimation_.IsReadyForFade() && !lifePresentationStarted_ && Fade::GetInstance()->GetStatus() == Fade::Status::None) {
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
        cam->SetFreezeEye(false);
    }

    if (GameDataManager::GetInstance()->GetLives() <= 0) {
        Fade::GetInstance()->Stop();
        SceneManager::GetInstance()->ChangeScene("GAMEOVER");
    } else {
        GameDataManager::GetInstance()->RequestRespawnIrisIn();
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
    }
}
void PlayerStateDead::Exit(Player* player) {
    if (player) {
        deathAnimation_.RestoreVisual(player);
        player->SetIsControlActive(true);
    }
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (cam) {
        cam->SetFreezeEye(false);
    }
}



// ========================================================
// 落下演出状態 (FallingOut)
// ========================================================
void PlayerStateFallingOut::Enter(Player* player) {
    if (!player) return;
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
    if (!player) return;

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
            SceneManager::GetInstance()->ChangeScene("GAMEOVER");
            return;
        }

        player->SetTranslate(player->GetRespawnPosition());
        player->SetVelocity({ 0,0,0 });

        CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
        CameraManager::GetInstance()->GetActiveCamera()->Update();
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
    if (player) {
        player->SetIsControlActive(true);
    }
    CameraManager::GetInstance()->GetActiveCamera()->SetFreezeEye(false);
}

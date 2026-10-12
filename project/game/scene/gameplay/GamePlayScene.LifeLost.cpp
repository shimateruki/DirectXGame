#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"

#include "CameraManager.h"
#include "Sprite.h"
#include "WinApp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

// 残機減少時の暗転、数字演出、専用カメラを一箇所で管理します。

void GamePlayScene::StartLifeLostPresentation(int beforeLives, int afterLives) {
    // 演出開始時の表示数を固定し、途中でセーブ値が変わっても数字を跳ねさせません。
    lifeLostPresentationActive_ = true;
    lifeLostPresentationFinished_ = false;
    lifeLostBlackHold_ = true;
    lifeLostNumberDropped_ = false;
    lifeLostRevive_ = afterLives > 0;
    lifeLostPresentationTimer_ = 0.0f;
    lifeLostBeforeLives_ = std::clamp(beforeLives, 0, 99);
    lifeLostAfterLives_ = std::clamp(afterLives, 0, 99);
    lifeLostIrisCenter_ = {0.5f, 0.5f};
    if (lifeLostSlimeObject_) {
        lifeLostSlimeObject_->SetIsVisible(false);
    }
    if (lifeLostStunObject_) {
        lifeLostStunObject_->SetIsVisible(false);
    }

    // 暗転の中心は、切り替え前のカメラで見たプレイヤー位置に合わせます。
    if (player_) {
        Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
        if (cam) {
            Vector3 worldPos = player_->GetWorldPosition();
            worldPos.y += 1.0f;
            Vector3 ndc = Math::Transform(worldPos, cam->GetViewProjectionMatrix());
            lifeLostIrisCenter_ = {std::clamp((ndc.x + 1.0f) * 0.5f, 0.08f, 0.92f),
                                   std::clamp((1.0f - ndc.y) * 0.5f, 0.08f, 0.92f)};
        }
    }
    if (lifeLostCamera_) {
        CameraManager::GetInstance()->SetActiveCamera(lifeLostCamera_.get());
    }
}

// シーン遷移や復帰で演出が中断されても、専用カメラと表示物を残しません。
void GamePlayScene::HideLifeLostPresentationOverlay() {
    // 残機減少演出のカメラとSpriteを片付け、通常シーンへ描画を戻す。
    lifeLostPresentationActive_ = false;
    lifeLostPresentationFinished_ = true;
    lifeLostBlackHold_ = false;
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    if (lifeLostIcon_.sprite)
        lifeLostIcon_.sprite->SetVisible(false);
    if (lifeLostXIcon_.sprite)
        lifeLostXIcon_.sprite->SetVisible(false);
    if (lifeLostBackdrop_.sprite)
        lifeLostBackdrop_.sprite->SetVisible(false);
    if (lifeLostSlimeObject_)
        lifeLostSlimeObject_->SetIsVisible(false);
    if (lifeLostStunObject_)
        lifeLostStunObject_->SetIsVisible(false);
    for (auto& digit : lifeLostDigits_) {
        if (digit.sprite)
            digit.sprite->SetVisible(false);
    }
}

void GamePlayScene::UpdateLifeLostPresentation(float deltaTime) {
    if (!lifeLostPresentationActive_) {
        // 演出完了後もシーン切り替えまで黒画面を保持します。
        if (lifeLostBlackHold_) {
            const float screenW = static_cast<float>(WinApp::kClientWidth);
            const float screenH = static_cast<float>(WinApp::kClientHeight);
            if (lifeLostBackdrop_.sprite) {
                lifeLostBackdrop_.sprite->SetVisible(true);
                lifeLostBackdrop_.sprite->SetPosition({screenW * 0.5f, screenH * 0.5f});
                lifeLostBackdrop_.sprite->SetSize({screenW, screenH});
                lifeLostBackdrop_.sprite->SetColor({0.0f, 0.0f, 0.0f, 1.0f});
                lifeLostBackdrop_.sprite->Update();
            }
            if (lifeLostIcon_.sprite)
                lifeLostIcon_.sprite->SetVisible(false);
            if (lifeLostXIcon_.sprite)
                lifeLostXIcon_.sprite->SetVisible(false);
            for (auto& digit : lifeLostDigits_) {
                if (digit.sprite)
                    digit.sprite->SetVisible(false);
            }
            return;
        }
        if (lifeLostIcon_.sprite)
            lifeLostIcon_.sprite->SetVisible(false);
        if (lifeLostXIcon_.sprite)
            lifeLostXIcon_.sprite->SetVisible(false);
        if (lifeLostBackdrop_.sprite)
            lifeLostBackdrop_.sprite->SetVisible(false);
        for (auto& digit : lifeLostDigits_) {
            if (digit.sprite)
                digit.sprite->SetVisible(false);
        }
        return;
    }

    lifeLostPresentationTimer_ += deltaTime;
    UpdateLifeLostPresentationWorld(deltaTime);
    const float t = lifeLostPresentationTimer_;
    constexpr float kNumberDropTime = 1.28f;
    const float kFadeOutStartTime = lifeLostRevive_ ? 2.65f : 2.35f;
    constexpr float kFadeOutDuration = 0.35f;
    const float kEndTime = lifeLostRevive_ ? 3.12f : 2.85f;
    if (t >= kEndTime) {
        // 終端では描画を消して完了通知だけ残し、次のシーン更新に渡します。
        lifeLostPresentationActive_ = false;
        lifeLostPresentationFinished_ = true;
        lifeLostBlackHold_ = true;
        if (lifeLostIcon_.sprite)
            lifeLostIcon_.sprite->SetVisible(false);
        if (lifeLostXIcon_.sprite)
            lifeLostXIcon_.sprite->SetVisible(false);
        if (lifeLostSlimeObject_)
            lifeLostSlimeObject_->SetIsVisible(false);
        if (lifeLostStunObject_)
            lifeLostStunObject_->SetIsVisible(false);
        for (auto& digit : lifeLostDigits_) {
            if (digit.sprite)
                digit.sprite->SetVisible(false);
        }
        return;
    }

    if (t >= kNumberDropTime) {
        lifeLostNumberDropped_ = true;
    }

    float alpha = std::clamp(t / 0.25f, 0.0f, 1.0f);
    if (t > kFadeOutStartTime) {
        alpha = std::clamp(1.0f - (t - kFadeOutStartTime) / kFadeOutDuration, 0.0f, 1.0f);
    }

    const int displayLives = lifeLostNumberDropped_ ? lifeLostAfterLives_ : lifeLostBeforeLives_;
    const float screenW = static_cast<float>(WinApp::kClientWidth);
    const float screenH = static_cast<float>(WinApp::kClientHeight);
    const float centerX = screenW * 0.5f;
    const float centerY = screenH * 0.5f;
    const float settlePulse = lifeLostNumberDropped_
                                  ? 1.0f + std::max(0.0f, 1.0f - (t - kNumberDropTime) / 0.42f) * 0.22f
                                  : 1.0f + std::sin(t * 7.0f) * 0.035f;

    // 背景、アイコン、数字の順に更新して、数字の落下後だけ色と拡大率を切り替えます。
    if (lifeLostBackdrop_.sprite) {
        lifeLostBackdrop_.sprite->SetVisible(true);
        lifeLostBackdrop_.sprite->SetPosition({centerX, centerY});
        lifeLostBackdrop_.sprite->SetSize({screenW, screenH});
        lifeLostBackdrop_.sprite->SetColor({0.0f, 0.0f, 0.0f, 1.0f});
        lifeLostBackdrop_.sprite->Update();
    }
    if (lifeLostIcon_.sprite) {
        lifeLostIcon_.sprite->SetVisible(true);
        lifeLostIcon_.sprite->SetPosition(lifeLostIcon_.basePosition);
        lifeLostIcon_.sprite->SetSize({lifeLostIcon_.baseSize.x * settlePulse, lifeLostIcon_.baseSize.y * settlePulse});
        lifeLostIcon_.sprite->SetColor(
            {lifeLostIcon_.baseColor.x, lifeLostIcon_.baseColor.y, lifeLostIcon_.baseColor.z, alpha});
        lifeLostIcon_.sprite->Update();
    }
    if (lifeLostXIcon_.sprite) {
        lifeLostXIcon_.sprite->SetVisible(true);
        lifeLostXIcon_.sprite->SetPosition(lifeLostXIcon_.basePosition);
        lifeLostXIcon_.sprite->SetSize(lifeLostXIcon_.baseSize);
        lifeLostXIcon_.sprite->SetColor(
            {lifeLostXIcon_.baseColor.x, lifeLostXIcon_.baseColor.y, lifeLostXIcon_.baseColor.z, alpha});
        lifeLostXIcon_.sprite->Update();
    }

    // 1 桁のときは十の位を隠し、残る 1 枚を右寄せ位置へ配置します。
    const int tens = displayLives / 10;
    const int ones = displayLives % 10;
    const bool showTens = displayLives >= 10;
    const float digitHeight =
        (lifeLostDigits_[1].baseSize.y > 0.0f ? lifeLostDigits_[1].baseSize.y : 86.0f) * settlePulse;
    const float digitWidth = digitHeight * 0.68f;
    const float spacing = digitWidth * 0.82f;
    const float rightX = lifeLostDigits_[1].sprite ? lifeLostDigits_[1].basePosition.x : centerX + 132.0f;
    const float digitY = lifeLostDigits_[1].sprite ? lifeLostDigits_[1].basePosition.y : centerY + 2.0f;
    const float totalWidth = showTens ? spacing + digitWidth : digitWidth;
    const float startX = rightX - totalWidth + digitWidth * 0.5f;
    const std::array<int, 2> values = {tens, ones};

    for (int i = 0; i < 2; ++i) {
        Sprite* digit = lifeLostDigits_[i].sprite;
        if (!digit)
            continue;

        const bool digitVisible = showTens || i == 1;
        digit->SetVisible(digitVisible);
        if (!digitVisible)
            continue;

        const int sourceIndex = showTens ? i : 1;
        const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(values[sourceIndex]) + ".png");
        digit->SetTextureHandle(handle);
        digit->SetPosition({startX + (sourceIndex - (2 - (showTens ? 2 : 1))) * spacing, digitY});
        digit->SetSize({digitWidth, digitHeight});
        digit->SetColor(lifeLostNumberDropped_ ? Vector4{1.0f, 0.65f, 0.22f, alpha}
                                               : Vector4{1.0f, 0.92f, 0.38f, alpha});
        digit->Update();
    }
}

void GamePlayScene::DrawLifeLostPresentation() {
    // 暗転保持中も背景は描き、演出終了まで画面の飛びを防ぐ。
    if (!lifeLostPresentationActive_ && !lifeLostBlackHold_)
        return;
    if (!lifeLostPresentationActive_) {
        DrawGameplayHUDSprite(lifeLostBackdrop_);
    }
    if (!lifeLostPresentationActive_)
        return;
    DrawGameplayHUDSprite(lifeLostIcon_);
    DrawGameplayHUDSprite(lifeLostXIcon_);
    for (auto& digit : lifeLostDigits_) {
        DrawGameplayHUDSprite(digit);
    }
}

// ライフ減少専用の3D表示物は初期化時に用意し、演出中は表示だけ切り替えます。
void GamePlayScene::InitializeLifeLostPresentationObjects() {
    // 専用カメラとモデルは3D描画基盤の初期化後に生成する。
    if (!object3dCommon_) {
        return;
    }

    lifeLostCamera_ = std::make_unique<Camera>();
    lifeLostCamera_->Initialize();
    lifeLostCamera_->SetInputEnabled(false);
    lifeLostCamera_->SetFollowTarget(nullptr);
    lifeLostCamera_->SetFollowMode(Camera::FollowMode::kFixedPoint);
    lifeLostCamera_->SetFovY(0.48f);
    lifeLostCamera_->ConfigFixedPoint({0.0f, 2.25f, -7.2f}, {0.10f, 0.0f, 0.0f});
    lifeLostCamera_->Update();

    lifeLostSlimeObject_ = std::make_unique<Object3d>();
    lifeLostSlimeObject_->Initialize(object3dCommon_.get());
    lifeLostSlimeObject_->SetName("LifeLostSlime");
    lifeLostSlimeObject_->SetModel("Characters/slime");
    lifeLostSlimeObject_->SetBlendMode(BlendMode::kNormal);
    lifeLostSlimeObject_->SetEnableEnvMap(false);
    lifeLostSlimeObject_->SetEmissive(1.22f);
    lifeLostSlimeObject_->SetColor({0.38f, 0.92f, 1.0f, 1.0f});
    lifeLostSlimeObject_->SetIsVisible(false);

    lifeLostStunObject_ = std::make_unique<Object3d>();
    lifeLostStunObject_->Initialize(object3dCommon_.get());
    lifeLostStunObject_->SetName("LifeLostStun");
    lifeLostStunObject_->SetModel("Primitives/sphere");
    lifeLostStunObject_->SetMaterialType(18);
    lifeLostStunObject_->SetBlendMode(BlendMode::kNormal);
    lifeLostStunObject_->SetColor({1.0f, 0.94f, 0.28f, 0.78f});
    lifeLostStunObject_->SetIsVisible(false);
    if (lifeLostStunObject_->GetMeshRenderer() && lifeLostStunObject_->GetMeshRenderer()->GetWaterParamData()) {
        auto* stun = lifeLostStunObject_->GetMeshRenderer()->GetWaterParamData();
        stun->effectType = 0.0f;
        stun->effectScale = 0.86f;
        stun->effectSoftness = 0.72f;
        stun->effectIntensity = 0.55f;
        stun->billboardScale = 1.08f;
        stun->waveSpeed = 1.4f;
        stun->waveFrequency = 2.8f;
    }
}

void GamePlayScene::UpdateLifeLostPresentationWorld(float deltaTime) {
    // 演出時計からカメラとモデル姿勢を求め、ゲーム側の物理更新には依存しない。
    (void)deltaTime;
    const float t = lifeLostPresentationTimer_;

    if (lifeLostCamera_) {
        CameraManager::GetInstance()->SetActiveCamera(lifeLostCamera_.get());
        lifeLostCamera_->ConfigFixedPoint({0.0f, 2.25f + std::sin(t * 0.9f) * 0.04f, -7.2f}, {0.10f, 0.0f, 0.0f});
        lifeLostCamera_->Update();
    }

    if (lifeLostSlimeObject_) {
        lifeLostSlimeObject_->SetIsVisible(false);
    }

    if (lifeLostStunObject_) {
        lifeLostStunObject_->SetIsVisible(false);
    }
}

void GamePlayScene::DrawLifeLostPresentationWorld(ID3D12Resource* pointLightResource,
                                                  ID3D12Resource* spotLightResource) {
    // 残機演出が有効な間だけ専用ワールドを描画する。
    if (!lifeLostPresentationActive_) {
        return;
    }

    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    DrawGameplayHUDSprite(lifeLostBackdrop_);

    if (lifeLostSlimeObject_ && lifeLostSlimeObject_->GetIsVisible()) {
        object3dCommon_->SetGraphicsCommand();
        object3dCommon_->SetPipelineState(BlendMode::kNormal);
        lifeLostSlimeObject_->Draw(pointLightResource, spotLightResource);
    }

    if (lifeLostStunObject_ && lifeLostStunObject_->GetIsVisible()) {
        dxCommon_->UpdateGrabTexture();
        lifeLostStunObject_->DrawStunBind(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
    }
}

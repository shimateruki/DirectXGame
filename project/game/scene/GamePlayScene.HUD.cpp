#define NOMINMAX
#include "GamePlayScene.h"

#include "CameraManager.h"
#include "GameDataManager.h"
#include "Sprite.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>

void GamePlayScene::UpdateUI(float deltaTime) {
    UpdateGameplayHUD(deltaTime);
    UpdateLifeLostPresentation(deltaTime);
}

std::unique_ptr<Sprite> GamePlayScene::CreateGameplayHUDSprite(const std::string& texturePath, const Vector2& position, const Vector2& size, const Vector2& anchor, const Vector4& color) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteCommon_.get(), texturePath);
    sprite->SetPosition(position);
    sprite->SetSize(size);
    sprite->SetAnchorPoint(anchor);
    sprite->SetColor(color);
    sprite->SetVisible(true);
    sprite->Update();
    return sprite;
}

void GamePlayScene::InitializeGameplayHUD() {
    hudLifeMeter_ = CreateGameplayHUDSprite(
        "Resources/sprite/ui/hud/life_meter_6.png",
        { static_cast<float>(WinApp::kClientWidth) - 118.0f, 92.0f },
        { 138.0f, 138.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.96f }
    );
    hudLifeMeterDigit_ = CreateGameplayHUDSprite(
        "Resources/sprite/number/big6.png",
        { static_cast<float>(WinApp::kClientWidth) - 118.0f, 95.0f },
        { 52.0f, 76.0f },
        { 0.5f, 0.5f },
        { 1.0f, 0.88f, 0.20f, 1.0f }
    );
    hudLifeIcon_ = CreateGameplayHUDSprite(
        "Resources/sprite/title/slime_save_icon.png",
        { 38.0f, 100.0f },
        { 50.0f, 50.0f },
        { 0.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.96f }
    );
    hudLifeXIcon_ = CreateGameplayHUDSprite(
        "Resources/sprite/ui/hud/xUi.png",
        { 92.0f, 100.0f },
        { 44.0f, 44.0f },
        { 0.5f, 0.5f },
        { 1.0f, 0.96f, 0.62f, 0.96f }
    );

    for (auto& digit : hudLifeDigits_) {
        digit = CreateGameplayHUDSprite(
            "Resources/sprite/number/0.png",
            { 100.0f, 100.0f },
            { 26.0f, 38.0f },
            { 0.5f, 0.5f },
            { 1.0f, 0.95f, 0.56f, 1.0f }
        );
    }

    hudCoinIcon_ = CreateGameplayHUDSprite(
        "Resources/sprite/ui/hud/coin_icon.png",
        { 38.0f, 154.0f },
        { 48.0f, 48.0f },
        { 0.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.96f }
    );
    hudCoinXIcon_ = CreateGameplayHUDSprite(
        "Resources/sprite/ui/hud/xUi.png",
        { 92.0f, 154.0f },
        { 44.0f, 44.0f },
        { 0.5f, 0.5f },
        { 1.0f, 0.90f, 0.42f, 0.96f }
    );
    for (auto& digit : hudCoinDigits_) {
        digit = CreateGameplayHUDSprite(
            "Resources/sprite/number/0.png",
            { 100.0f, 154.0f },
            { 26.0f, 38.0f },
            { 0.5f, 0.5f },
            { 1.0f, 0.86f, 0.28f, 1.0f }
        );
    }
    lifeLostIcon_ = CreateGameplayHUDSprite(
        "Resources/sprite/title/slime_save_icon.png",
        { static_cast<float>(WinApp::kClientWidth) * 0.5f - 78.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
        { 96.0f, 96.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.0f }
    );
    lifeLostXIcon_ = CreateGameplayHUDSprite(
        "Resources/sprite/ui/hud/xUi.png",
        { static_cast<float>(WinApp::kClientWidth) * 0.5f + 10.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
        { 72.0f, 72.0f },
        { 0.5f, 0.5f },
        { 1.0f, 0.96f, 0.62f, 0.0f }
    );
    for (auto& digit : lifeLostDigits_) {
        digit = CreateGameplayHUDSprite(
            "Resources/sprite/number/big0.png",
            { static_cast<float>(WinApp::kClientWidth) * 0.5f + 88.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
            { 56.0f, 82.0f },
            { 0.5f, 0.5f },
            { 1.0f, 0.92f, 0.38f, 0.0f }
        );
    }
    lifeLostBackdrop_ = CreateGameplayHUDSprite(
        "Resources/sprite/common/white.png",
        { static_cast<float>(WinApp::kClientWidth) * 0.5f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
        { static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) },
        { 0.5f, 0.5f },
        { 0.0f, 0.0f, 0.0f, 0.0f }
    );
    lifeLostPresentationActive_ = false;
    lifeLostPresentationFinished_ = true;
    lifeLostBlackHold_ = false;
    lifeLostNumberDropped_ = false;
    lifeLostPresentationTimer_ = 0.0f;

    hudPreviousHp_ = player_ ? player_->GetHp() : 0.0f;
    hudDamagePulseTimer_ = 0.0f;
    hudDisplayedLife_ = 6;
    UpdateGameplayHUD(0.0f);
}

void GamePlayScene::SetGameplayHUDNumber(std::array<std::unique_ptr<Sprite>, 2>& digits, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color, bool visible) {
    value = std::clamp(value, 0, 99);

    const std::array<int, 2> digitValues = { value / 10, value % 10 };
    const int digitCount = value >= 10 ? 2 : 1;
    const float digitWidth = digitHeight * 0.68f;
    const float spacing = digitWidth * 0.82f;
    const float totalWidth = digitCount == 2 ? spacing + digitWidth : digitWidth;
    const float startX = rightAlignedPosition.x - totalWidth + digitWidth * 0.5f;

    for (int i = 0; i < 2; ++i) {
        Sprite* sprite = digits[i].get();
        if (!sprite) continue;

        const bool digitVisible = visible && (digitCount == 2 || i == 1);
        sprite->SetVisible(digitVisible);
        if (!digitVisible) {
            continue;
        }

        const int sourceIndex = digitCount == 2 ? i : 1;
        const int digit = digitValues[sourceIndex];
        const uint32_t handle = Sprite::LoadTexture("number/" + std::to_string(digit) + ".png");
        sprite->SetTextureHandle(handle);
        sprite->SetPosition({ startX + (sourceIndex - (2 - digitCount)) * spacing, rightAlignedPosition.y });
        sprite->SetSize({ digitWidth, digitHeight });
        sprite->SetColor(color);
        sprite->Update();
    }
}

void GamePlayScene::UpdateGameplayHUD(float deltaTime) {
    const bool visible = player_ != nullptr;
    const float maxHp = player_ ? std::max(player_->GetMaxHp(), 1.0f) : 1.0f;
    const float hp = player_ ? std::clamp(player_->GetHp(), 0.0f, maxHp) : 0.0f;
    const float hpRate = hp / maxHp;
    int lifeValue = hp <= 0.0f ? 0 : static_cast<int>(std::ceil(hpRate * 6.0f));
    lifeValue = std::clamp(lifeValue, 0, 6);

    if (visible && (hp < hudPreviousHp_ - 0.01f || lifeValue != hudDisplayedLife_)) {
        hudDamagePulseTimer_ = 0.28f;
    }
    hudDisplayedLife_ = lifeValue;
    hudPreviousHp_ = hp;
    hudDamagePulseTimer_ = std::max(0.0f, hudDamagePulseTimer_ - deltaTime);

    const float pulse = hudDamagePulseTimer_ > 0.0f ? std::sin(hudDamagePulseTimer_ * 70.0f) : 0.0f;
    const float lifePulse = hudDamagePulseTimer_ > 0.0f ? 1.0f + std::abs(pulse) * 0.08f : 1.0f;
    const Vector2 meterCenter = { static_cast<float>(WinApp::kClientWidth) - 118.0f, 92.0f };

    if (hudLifeMeter_) {
        const uint32_t handle = Sprite::LoadTexture("ui/hud/life_meter_" + std::to_string(lifeValue) + ".png");
        hudLifeMeter_->SetTextureHandle(handle);
        hudLifeMeter_->SetVisible(visible);
        hudLifeMeter_->SetPosition(meterCenter);
        hudLifeMeter_->SetSize({ 138.0f * lifePulse, 138.0f * lifePulse });
        hudLifeMeter_->SetRotation(pulse * 0.03f);
        hudLifeMeter_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? 0.96f : 0.0f });
        hudLifeMeter_->Update();
    }
    if (hudLifeMeterDigit_) {
        const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(lifeValue) + ".png");
        hudLifeMeterDigit_->SetTextureHandle(handle);
        hudLifeMeterDigit_->SetVisible(visible);
        hudLifeMeterDigit_->SetPosition({ meterCenter.x, meterCenter.y + 3.0f });
        hudLifeMeterDigit_->SetSize({ 52.0f * lifePulse, 76.0f * lifePulse });
        hudLifeMeterDigit_->SetColor(lifeValue <= 1 ? Vector4{ 1.0f, 0.35f, 0.25f, 1.0f } : Vector4{ 1.0f, 0.88f, 0.20f, 1.0f });
        hudLifeMeterDigit_->Update();
    }
    if (hudLifeIcon_) {
        hudLifeIcon_->SetVisible(visible);
        hudLifeIcon_->SetPosition({ 38.0f, 100.0f });
        hudLifeIcon_->SetSize({ 50.0f * lifePulse, 50.0f * lifePulse });
        hudLifeIcon_->SetColor({ 1.0f, 1.0f, 1.0f, 0.96f });
        hudLifeIcon_->Update();
    }
    if (hudLifeXIcon_) {
        hudLifeXIcon_->SetVisible(visible);
        hudLifeXIcon_->SetPosition({ 92.0f, 100.0f });
        hudLifeXIcon_->SetSize({ 44.0f, 44.0f });
        hudLifeXIcon_->SetColor({ 1.0f, 0.96f, 0.62f, visible ? 0.96f : 0.0f });
        hudLifeXIcon_->Update();
    }

    const int lives = GameDataManager::GetInstance()->GetLives();
    SetGameplayHUDNumber(
        hudLifeDigits_,
        lives,
        { 162.0f, 100.0f },
        40.0f,
        { 1.0f, 0.95f, 0.56f, 1.0f },
        visible
    );

    if (hudCoinIcon_) {
        hudCoinIcon_->SetVisible(visible);
        hudCoinIcon_->SetPosition({ 38.0f, 154.0f });
        hudCoinIcon_->SetSize({ 48.0f, 48.0f });
        hudCoinIcon_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? 0.96f : 0.0f });
        hudCoinIcon_->Update();
    }
    if (hudCoinXIcon_) {
        hudCoinXIcon_->SetVisible(visible);
        hudCoinXIcon_->SetPosition({ 92.0f, 154.0f });
        hudCoinXIcon_->SetSize({ 44.0f, 44.0f });
        hudCoinXIcon_->SetColor({ 1.0f, 0.90f, 0.42f, visible ? 0.96f : 0.0f });
        hudCoinXIcon_->Update();
    }

    const int coins = GameDataManager::GetInstance()->GetCoins();
    SetGameplayHUDNumber(
        hudCoinDigits_,
        coins,
        { 162.0f, 154.0f },
        40.0f,
        { 1.0f, 0.86f, 0.28f, 1.0f },
        visible
    );
}

void GamePlayScene::StartLifeLostPresentation(int beforeLives, int afterLives) {
    lifeLostPresentationActive_ = true;
    lifeLostPresentationFinished_ = false;
    lifeLostBlackHold_ = true;
    lifeLostNumberDropped_ = false;
    lifeLostPresentationTimer_ = 0.0f;
    lifeLostBeforeLives_ = std::clamp(beforeLives, 0, 99);
    lifeLostAfterLives_ = std::clamp(afterLives, 0, 99);
    lifeLostIrisCenter_ = { 0.5f, 0.5f };
    if (player_) {
        Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
        if (cam) {
            Vector3 worldPos = player_->GetWorldPosition();
            worldPos.y += 1.0f;
            Vector3 ndc = Math::Transform(worldPos, cam->GetViewProjectionMatrix());
            lifeLostIrisCenter_ = {
                std::clamp((ndc.x + 1.0f) * 0.5f, 0.08f, 0.92f),
                std::clamp((1.0f - ndc.y) * 0.5f, 0.08f, 0.92f)
            };
        }
    }
}

void GamePlayScene::HideLifeLostPresentationOverlay() {
    lifeLostPresentationActive_ = false;
    lifeLostPresentationFinished_ = true;
    lifeLostBlackHold_ = false;
    if (lifeLostIcon_) lifeLostIcon_->SetVisible(false);
    if (lifeLostXIcon_) lifeLostXIcon_->SetVisible(false);
    if (lifeLostBackdrop_) lifeLostBackdrop_->SetVisible(false);
    for (auto& digit : lifeLostDigits_) {
        if (digit) digit->SetVisible(false);
    }
}

void GamePlayScene::UpdateLifeLostPresentation(float deltaTime) {
    if (!lifeLostPresentationActive_) {
        if (lifeLostBlackHold_) {
            const float screenW = static_cast<float>(WinApp::kClientWidth);
            const float screenH = static_cast<float>(WinApp::kClientHeight);
            if (lifeLostBackdrop_) {
                lifeLostBackdrop_->SetVisible(true);
                lifeLostBackdrop_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
                lifeLostBackdrop_->SetSize({ screenW, screenH });
                lifeLostBackdrop_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
                lifeLostBackdrop_->Update();
            }
            if (lifeLostIcon_) lifeLostIcon_->SetVisible(false);
            if (lifeLostXIcon_) lifeLostXIcon_->SetVisible(false);
            for (auto& digit : lifeLostDigits_) {
                if (digit) digit->SetVisible(false);
            }
            return;
        }
        if (lifeLostIcon_) lifeLostIcon_->SetVisible(false);
        if (lifeLostXIcon_) lifeLostXIcon_->SetVisible(false);
        if (lifeLostBackdrop_) lifeLostBackdrop_->SetVisible(false);
        for (auto& digit : lifeLostDigits_) {
            if (digit) digit->SetVisible(false);
        }
        return;
    }

    lifeLostPresentationTimer_ += deltaTime;
    const float t = lifeLostPresentationTimer_;
    constexpr float kNumberDropTime = 1.15f;
    constexpr float kFadeOutStartTime = 2.35f;
    constexpr float kFadeOutDuration = 0.35f;
    constexpr float kEndTime = 2.85f;
    if (t >= kEndTime) {
        lifeLostPresentationActive_ = false;
        lifeLostPresentationFinished_ = true;
        lifeLostBlackHold_ = true;
        if (lifeLostIcon_) lifeLostIcon_->SetVisible(false);
        if (lifeLostXIcon_) lifeLostXIcon_->SetVisible(false);
        for (auto& digit : lifeLostDigits_) {
            if (digit) digit->SetVisible(false);
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

    if (lifeLostBackdrop_) {
        lifeLostBackdrop_->SetVisible(true);
        lifeLostBackdrop_->SetPosition({ centerX, centerY });
        lifeLostBackdrop_->SetSize({ screenW, screenH });
        lifeLostBackdrop_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        lifeLostBackdrop_->Update();
    }

    if (lifeLostIcon_) {
        lifeLostIcon_->SetVisible(true);
        lifeLostIcon_->SetPosition({ centerX - 82.0f, centerY });
        lifeLostIcon_->SetSize({ 96.0f * settlePulse, 96.0f * settlePulse });
        lifeLostIcon_->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
        lifeLostIcon_->Update();
    }
    if (lifeLostXIcon_) {
        lifeLostXIcon_->SetVisible(true);
        lifeLostXIcon_->SetPosition({ centerX + 6.0f, centerY + 3.0f });
        lifeLostXIcon_->SetSize({ 72.0f, 72.0f });
        lifeLostXIcon_->SetColor({ 1.0f, 0.95f, 0.58f, alpha });
        lifeLostXIcon_->Update();
    }

    const int tens = displayLives / 10;
    const int ones = displayLives % 10;
    const bool showTens = displayLives >= 10;
    const float digitHeight = 86.0f * settlePulse;
    const float digitWidth = digitHeight * 0.68f;
    const float spacing = digitWidth * 0.82f;
    const float rightX = centerX + 132.0f;
    const float totalWidth = showTens ? spacing + digitWidth : digitWidth;
    const float startX = rightX - totalWidth + digitWidth * 0.5f;
    const std::array<int, 2> values = { tens, ones };

    for (int i = 0; i < 2; ++i) {
        Sprite* digit = lifeLostDigits_[i].get();
        if (!digit) continue;

        const bool digitVisible = showTens || i == 1;
        digit->SetVisible(digitVisible);
        if (!digitVisible) continue;

        const int sourceIndex = showTens ? i : 1;
        const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(values[sourceIndex]) + ".png");
        digit->SetTextureHandle(handle);
        digit->SetPosition({ startX + (sourceIndex - (2 - (showTens ? 2 : 1))) * spacing, centerY + 2.0f });
        digit->SetSize({ digitWidth, digitHeight });
        digit->SetColor(lifeLostNumberDropped_
            ? Vector4{ 1.0f, 0.65f, 0.22f, alpha }
            : Vector4{ 1.0f, 0.92f, 0.38f, alpha });
        digit->Update();
    }
}

void GamePlayScene::DrawGameplayHUD() {
    if (hudLifeIcon_) hudLifeIcon_->Draw();
    if (hudLifeXIcon_) hudLifeXIcon_->Draw();
    for (auto& digit : hudLifeDigits_) {
        if (digit) digit->Draw();
    }
    if (hudCoinIcon_) hudCoinIcon_->Draw();
    if (hudCoinXIcon_) hudCoinXIcon_->Draw();
    for (auto& digit : hudCoinDigits_) {
        if (digit) digit->Draw();
    }
    if (hudLifeMeter_) hudLifeMeter_->Draw();
    if (hudLifeMeterDigit_) hudLifeMeterDigit_->Draw();
    DrawLifeLostPresentation();
}

void GamePlayScene::DrawLifeLostPresentation() {
    if (!lifeLostPresentationActive_ && !lifeLostBlackHold_) return;
    if (lifeLostBackdrop_) lifeLostBackdrop_->Draw();
    if (!lifeLostPresentationActive_) return;
    if (lifeLostIcon_) lifeLostIcon_->Draw();
    if (lifeLostXIcon_) lifeLostXIcon_->Draw();
    for (auto& digit : lifeLostDigits_) {
        if (digit) digit->Draw();
    }
}

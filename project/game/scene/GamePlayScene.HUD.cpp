#define NOMINMAX
#include "GamePlayScene.h"

#include "CameraManager.h"
#include "GameDataManager.h"
#include "LevelLoader.h"
#include "Sprite.h"
#include "StageManager.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPi = 3.1415926535f;

float SmoothStep(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

Vector2 Lerp(const Vector2& a, const Vector2& b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}

Vector2 QuadraticBezier(const Vector2& start, const Vector2& control, const Vector2& end, float t) {
    return Lerp(Lerp(start, control, t), Lerp(control, end, t), t);
}
}

void GamePlayScene::UpdateUI(float deltaTime) {
    UpdateGameplayHUD(deltaTime);
    UpdateLifeLostPresentation(deltaTime);
}

GamePlayScene::HudSpriteState GamePlayScene::BindGameplayHUDSprite(const std::string& name, const std::string& texturePath, const Vector2& position, const Vector2& size, const Vector2& anchor, const Vector4& color) {
    Sprite* sprite = GetSpriteByName(name);
    bool createdFromFallback = false;
    if (!sprite) {
        auto newSprite = std::make_unique<Sprite>();
        newSprite->Initialize(spriteCommon_.get(), texturePath);
        newSprite->SetName(name);
        newSprite->SetTextureName(texturePath.rfind("Resources/sprite/", 0) == 0 ? texturePath.substr(std::string("Resources/sprite/").size()) : texturePath);
        sprite = newSprite.get();
        sprites_.push_back(std::move(newSprite));
        createdFromFallback = true;
    }

    if (sprite->GetSize().x <= 0.0f || sprite->GetSize().y <= 0.0f) {
        sprite->SetSize(size);
    }
    if (sprite->GetTextureName().empty()) {
        sprite->SetTextureName(texturePath.rfind("Resources/sprite/", 0) == 0 ? texturePath.substr(std::string("Resources/sprite/").size()) : texturePath);
    }

    // JSONに存在しない場合だけフォールバック値で配置します。
    if (createdFromFallback) {
        sprite->SetPosition(position);
        sprite->SetAnchorPoint(anchor);
        sprite->SetColor(color);
    }
    sprite->SetVisible(true);
    sprite->Update();

    return { sprite, sprite->GetPosition(), sprite->GetSize(), sprite->GetColor() };
}

void GamePlayScene::DrawGameplayHUDSprite(const HudSpriteState& state) {
    if (state.sprite) {
        state.sprite->Draw();
    }
}

bool GamePlayScene::IsGameplayHUDSprite(const Sprite* sprite) const {
    if (!sprite) {
        return false;
    }

    const std::string& name = sprite->GetName();
    return name.rfind("hud_", 0) == 0 || name.rfind("life_lost_", 0) == 0;
}

void GamePlayScene::InitializeGameplayHUD() {
    if (levelLoader_) {
        levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/gameplayHUD.json");
    }

    hudLifeMeter_ = BindGameplayHUDSprite(
        "hud_life_meter",
        "Resources/sprite/ui/hud/life_meter_6.png",
        { static_cast<float>(WinApp::kClientWidth) - 118.0f, 92.0f },
        { 138.0f, 138.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.96f }
    );
    hudLifeMeterDigit_ = BindGameplayHUDSprite(
        "hud_life_meter_digit",
        "Resources/sprite/number/big6.png",
        { static_cast<float>(WinApp::kClientWidth) - 118.0f, 95.0f },
        { 52.0f, 76.0f },
        { 0.5f, 0.5f },
        { 1.0f, 0.88f, 0.20f, 1.0f }
    );
    hudLifeIcon_ = BindGameplayHUDSprite(
        "hud_life_icon",
        "Resources/sprite/title/slime_save_icon.png",
        { 38.0f, 100.0f },
        { 50.0f, 50.0f },
        { 0.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.96f }
    );
    hudLifeXIcon_ = BindGameplayHUDSprite(
        "hud_life_x",
        "Resources/sprite/ui/hud/xUi.png",
        { 92.0f, 100.0f },
        { 44.0f, 44.0f },
        { 0.5f, 0.5f },
        { 1.0f, 0.96f, 0.62f, 0.96f }
    );

    for (size_t i = 0; i < hudLifeDigits_.size(); ++i) {
        hudLifeDigits_[i] = BindGameplayHUDSprite(
            "hud_life_digit_" + std::to_string(i),
            "Resources/sprite/number/0.png",
            { 162.0f, 100.0f },
            { 26.0f, 38.0f },
            { 0.5f, 0.5f },
            { 1.0f, 0.95f, 0.56f, 1.0f }
        );
    }

    hudCoinIcon_ = BindGameplayHUDSprite(
        "hud_coin_icon",
        "Resources/sprite/ui/hud/coin_icon.png",
        { 38.0f, 154.0f },
        { 48.0f, 48.0f },
        { 0.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.96f }
    );
    hudCoinXIcon_ = BindGameplayHUDSprite(
        "hud_coin_x",
        "Resources/sprite/ui/hud/xUi.png",
        { 92.0f, 154.0f },
        { 44.0f, 44.0f },
        { 0.5f, 0.5f },
        { 1.0f, 0.90f, 0.42f, 0.96f }
    );
    for (size_t i = 0; i < hudCoinDigits_.size(); ++i) {
        hudCoinDigits_[i] = BindGameplayHUDSprite(
            "hud_coin_digit_" + std::to_string(i),
            "Resources/sprite/number/0.png",
            { 162.0f, 154.0f },
            { 26.0f, 38.0f },
            { 0.5f, 0.5f },
            { 1.0f, 0.86f, 0.28f, 1.0f }
        );
    }
    const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
    for (size_t i = 0; i < hudStageStarSlots_.size(); ++i) {
        hudStageStarSlots_[i] = BindGameplayHUDSprite(
            "hud_stage_star_slot_" + std::to_string(i),
            "Resources/sprite/ui/hud/stage_star_empty.png",
            { 52.0f + 48.0f * static_cast<float>(i), 214.0f },
            { 42.0f, 42.0f },
            { 0.5f, 0.5f },
            { 1.0f, 1.0f, 1.0f, 0.94f }
        );
        hudStageStarVisualCollected_[i] =
            sessionStarCoins_[i] ||
            GameDataManager::GetInstance()->IsStarCoinCollected(stageIndex, static_cast<int>(i));
        hudStageStarPulseTimers_[i] = 0.0f;
    }
    hudStageStarFlyParticles_.clear();
    hudPreviousLives_ = GameDataManager::GetInstance()->GetLives();
    hudPreviousCoins_ = GameDataManager::GetInstance()->GetCoins();
    hudLifeGainPulseTimer_ = 0.0f;
    hudCoinPulseTimer_ = 0.0f;
    lifeLostIcon_ = BindGameplayHUDSprite(
        "life_lost_icon",
        "Resources/sprite/title/slime_save_icon.png",
        { static_cast<float>(WinApp::kClientWidth) * 0.5f - 78.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
        { 96.0f, 96.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.0f }
    );
    lifeLostXIcon_ = BindGameplayHUDSprite(
        "life_lost_x",
        "Resources/sprite/ui/hud/xUi.png",
        { static_cast<float>(WinApp::kClientWidth) * 0.5f + 10.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
        { 72.0f, 72.0f },
        { 0.5f, 0.5f },
        { 1.0f, 0.96f, 0.62f, 0.0f }
    );
    for (size_t i = 0; i < lifeLostDigits_.size(); ++i) {
        lifeLostDigits_[i] = BindGameplayHUDSprite(
            "life_lost_digit_" + std::to_string(i),
            "Resources/sprite/number/big0.png",
            { static_cast<float>(WinApp::kClientWidth) * 0.5f + 88.0f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
            { 56.0f, 82.0f },
            { 0.5f, 0.5f },
            { 1.0f, 0.92f, 0.38f, 0.0f }
        );
    }
    lifeLostBackdrop_ = BindGameplayHUDSprite(
        "life_lost_backdrop",
        "Resources/sprite/common/white.png",
        { static_cast<float>(WinApp::kClientWidth) * 0.5f, static_cast<float>(WinApp::kClientHeight) * 0.5f },
        { static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) },
        { 0.5f, 0.5f },
        { 0.0f, 0.0f, 0.0f, 0.0f }
    );
    InitializeLifeLostPresentationObjects();

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

void GamePlayScene::SetGameplayHUDNumber(std::array<HudSpriteState, 2>& digits, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color, bool visible) {
    value = std::clamp(value, 0, 99);

    const std::array<int, 2> digitValues = { value / 10, value % 10 };
    const int digitCount = value >= 10 ? 2 : 1;
    const float digitWidth = digitHeight * 0.68f;
    const float spacing = digitWidth * 0.82f;
    const float totalWidth = digitCount == 2 ? spacing + digitWidth : digitWidth;
    const float startX = rightAlignedPosition.x - totalWidth + digitWidth * 0.5f;

    for (int i = 0; i < 2; ++i) {
        Sprite* sprite = digits[i].sprite;
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

void GamePlayScene::CollectStarCoin(int coinIndex) {
    if (coinIndex < 0 || coinIndex >= 3) {
        return;
    }

    sessionStarCoins_[coinIndex] = true;
    hudStageStarVisualCollected_[coinIndex] = true;
    hudStageStarPulseTimers_[coinIndex] = 0.45f;
}

void GamePlayScene::CollectStarCoin(int coinIndex, const Vector3& worldPosition) {
    if (coinIndex < 0 || coinIndex >= 3) {
        return;
    }

    const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
    const bool alreadyCollected =
        sessionStarCoins_[coinIndex] ||
        GameDataManager::GetInstance()->IsStarCoinCollected(stageIndex, coinIndex);

    sessionStarCoins_[coinIndex] = true;
    if (alreadyCollected) {
        hudStageStarVisualCollected_[coinIndex] = true;
        hudStageStarPulseTimers_[coinIndex] = 0.45f;
        return;
    }

    hudStageStarVisualCollected_[coinIndex] = false;
    StartStageStarHUDCollectEffect(coinIndex, worldPosition);
}

Vector2 GamePlayScene::ProjectWorldToScreen(const Vector3& worldPosition) const {
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    const float screenW = static_cast<float>(WinApp::kClientWidth);
    const float screenH = static_cast<float>(WinApp::kClientHeight);
    if (!camera) {
        return { screenW * 0.5f, screenH * 0.5f };
    }

    Vector3 ndc = Math::Transform(worldPosition, camera->GetViewProjectionMatrix());
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y)) {
        return { screenW * 0.5f, screenH * 0.5f };
    }

    return {
        std::clamp((ndc.x + 1.0f) * 0.5f * screenW, 24.0f, screenW - 24.0f),
        std::clamp((1.0f - ndc.y) * 0.5f * screenH, 24.0f, screenH - 24.0f)
    };
}

void GamePlayScene::StartStageStarHUDCollectEffect(int starIndex, const Vector3& worldPosition) {
    if (starIndex < 0 || starIndex >= static_cast<int>(hudStageStarSlots_.size()) || !spriteCommon_) {
        if (starIndex >= 0 && starIndex < 3) {
            hudStageStarVisualCollected_[starIndex] = true;
            hudStageStarPulseTimers_[starIndex] = 0.45f;
        }
        return;
    }

    const Vector2 start = ProjectWorldToScreen(worldPosition);
    const Vector2 end = hudStageStarSlots_[starIndex].sprite
        ? hudStageStarSlots_[starIndex].basePosition
        : Vector2{ 52.0f + 48.0f * static_cast<float>(starIndex), 214.0f };

    auto addParticle = [&](const std::string& texturePath, const Vector2& particleStart, const Vector2& particleEnd, const Vector2& control, float duration, float baseSize, float rotationSpeed, bool fillsSlot) {
        StageStarUIFlyParticle particle;
        particle.sprite = std::make_unique<Sprite>();
        particle.sprite->Initialize(spriteCommon_.get(), texturePath);
        particle.sprite->SetAnchorPoint({ 0.5f, 0.5f });
        particle.sprite->SetPosition(particleStart);
        particle.sprite->SetSize({ baseSize, baseSize });
        particle.sprite->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        particle.sprite->SetVisible(true);
        particle.sprite->Update();
        particle.start = particleStart;
        particle.control = control;
        particle.end = particleEnd;
        particle.duration = duration;
        particle.baseSize = baseSize;
        particle.rotationSpeed = rotationSpeed;
        particle.starIndex = starIndex;
        particle.fillsSlot = fillsSlot;
        hudStageStarFlyParticles_.push_back(std::move(particle));
    };

    const Vector2 mainControl = {
        (start.x + end.x) * 0.5f,
        std::min(start.y, end.y) - 135.0f - 18.0f * static_cast<float>(starIndex)
    };
    addParticle("Resources/sprite/ui/hud/stage_star_filled.png", start, end, mainControl, 0.72f, 44.0f, 5.6f, true);

    for (int i = 0; i < 10; ++i) {
        const float angle = static_cast<float>(i) / 10.0f * kPi * 2.0f;
        const float radius = 18.0f + static_cast<float>((i % 3) * 7);
        const Vector2 startOffset = { static_cast<float>(std::cos(angle) * radius), static_cast<float>(std::sin(angle) * radius) };
        const Vector2 endOffset = { static_cast<float>(std::cos(angle + 0.9f) * 14.0f), static_cast<float>(std::sin(angle + 0.9f) * 14.0f) };
        const float lane = static_cast<float>(i - 5);
        const Vector2 control = {
            (start.x + end.x) * 0.5f + lane * 14.0f,
            std::min(start.y, end.y) - 82.0f - std::abs(lane) * 4.0f
        };
        addParticle(
            "Resources/sprite/ui/hud/stage_star_spark.png",
            { start.x + startOffset.x, start.y + startOffset.y },
            { end.x + endOffset.x, end.y + endOffset.y },
            control,
            0.48f + 0.035f * static_cast<float>(i % 4),
            14.0f + static_cast<float>(i % 3) * 3.0f,
            6.5f + static_cast<float>(i % 5),
            false
        );
    }
}

void GamePlayScene::UpdateStageStarHUD(float deltaTime, bool visible) {
    const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();

    for (auto it = hudStageStarFlyParticles_.begin(); it != hudStageStarFlyParticles_.end();) {
        StageStarUIFlyParticle& particle = *it;
        particle.timer += deltaTime;
        const float t = particle.duration > 0.0f ? std::clamp(particle.timer / particle.duration, 0.0f, 1.0f) : 1.0f;
        const float eased = SmoothStep(t);
        const Vector2 position = QuadraticBezier(particle.start, particle.control, particle.end, eased);
        const float fade = particle.fillsSlot ? 1.0f : std::clamp(1.0f - (t - 0.62f) / 0.38f, 0.0f, 1.0f);
        const float size = particle.baseSize * (particle.fillsSlot ? (1.0f + (1.0f - t) * 0.20f) : (1.0f - t * 0.38f));

        if (particle.sprite) {
            particle.sprite->SetVisible(visible && fade > 0.0f);
            particle.sprite->SetPosition(position);
            particle.sprite->SetSize({ size, size });
            particle.sprite->SetRotation(particle.rotationSpeed * particle.timer);
            particle.sprite->SetColor({ 1.0f, 1.0f, 1.0f, visible ? fade : 0.0f });
            particle.sprite->Update();
        }

        if (t >= 1.0f) {
            if (particle.fillsSlot && particle.starIndex >= 0 && particle.starIndex < 3) {
                hudStageStarVisualCollected_[particle.starIndex] = true;
                hudStageStarPulseTimers_[particle.starIndex] = 0.65f;
            }
            it = hudStageStarFlyParticles_.erase(it);
        }
        else {
            ++it;
        }
    }

    for (int i = 0; i < static_cast<int>(hudStageStarSlots_.size()); ++i) {
        bool hasActiveFill = false;
        for (const auto& particle : hudStageStarFlyParticles_) {
            if (particle.fillsSlot && particle.starIndex == i) {
                hasActiveFill = true;
                break;
            }
        }

        if (sessionStarCoins_[i] && !hasActiveFill && !hudStageStarVisualCollected_[i]) {
            hudStageStarVisualCollected_[i] = true;
        }
        if (GameDataManager::GetInstance()->IsStarCoinCollected(stageIndex, i)) {
            hudStageStarVisualCollected_[i] = true;
        }

        HudSpriteState& slot = hudStageStarSlots_[i];
        if (!slot.sprite) {
            continue;
        }

        hudStageStarPulseTimers_[i] = std::max(0.0f, hudStageStarPulseTimers_[i] - deltaTime);
        const bool collected = hudStageStarVisualCollected_[i];
        const uint32_t handle = Sprite::LoadTexture(collected ? "ui/hud/stage_star_filled.png" : "ui/hud/stage_star_empty.png");
        const float pulseT = hudStageStarPulseTimers_[i] > 0.0f ? hudStageStarPulseTimers_[i] / 0.65f : 0.0f;
        const float scale = 1.0f + std::sin((1.0f - pulseT) * kPi) * pulseT * 0.28f;
        const float alpha = visible ? (collected ? slot.baseColor.w : slot.baseColor.w * 0.78f) : 0.0f;

        slot.sprite->SetTextureHandle(handle);
        slot.sprite->SetVisible(visible);
        slot.sprite->SetPosition(slot.basePosition);
        slot.sprite->SetSize({ slot.baseSize.x * scale, slot.baseSize.y * scale });
        slot.sprite->SetRotation(collected ? std::sin(hudStageStarPulseTimers_[i] * 12.0f) * pulseT * 0.10f : 0.0f);
        slot.sprite->SetColor({ slot.baseColor.x, slot.baseColor.y, slot.baseColor.z, alpha });
        slot.sprite->Update();
    }
}

void GamePlayScene::UpdateGameplayHUD(float deltaTime) {
    const bool visible = player_ != nullptr;
    const float maxHp = player_ ? std::max(player_->GetMaxHp(), 1.0f) : 1.0f;
    const float hp = player_ ? std::clamp(player_->GetHp(), 0.0f, maxHp) : 0.0f;
    const float hpRate = hp / maxHp;
    int lifeValue = hp <= 0.0f ? 0 : static_cast<int>(std::ceil(hpRate * 6.0f));
    lifeValue = std::clamp(lifeValue, 0, 6);
    const int lives = GameDataManager::GetInstance()->GetLives();
    const int coins = GameDataManager::GetInstance()->GetCoins();

    if (visible && lives > hudPreviousLives_) {
        hudLifeGainPulseTimer_ = 0.62f;
    }
    if (visible && coins != hudPreviousCoins_) {
        hudCoinPulseTimer_ = 0.24f;
    }
    hudPreviousLives_ = lives;
    hudPreviousCoins_ = coins;

    if (visible && (hp < hudPreviousHp_ - 0.01f || lifeValue != hudDisplayedLife_)) {
        hudDamagePulseTimer_ = 0.28f;
    }
    hudDisplayedLife_ = lifeValue;
    hudPreviousHp_ = hp;
    hudDamagePulseTimer_ = std::max(0.0f, hudDamagePulseTimer_ - deltaTime);
    hudLifeGainPulseTimer_ = std::max(0.0f, hudLifeGainPulseTimer_ - deltaTime);
    hudCoinPulseTimer_ = std::max(0.0f, hudCoinPulseTimer_ - deltaTime);

    const float pulse = hudDamagePulseTimer_ > 0.0f ? std::sin(hudDamagePulseTimer_ * 70.0f) : 0.0f;
    const float lifePulse = hudDamagePulseTimer_ > 0.0f ? 1.0f + std::abs(pulse) * 0.08f : 1.0f;
    const float lifeGainRate = hudLifeGainPulseTimer_ > 0.0f ? hudLifeGainPulseTimer_ / 0.62f : 0.0f;
    const float lifeGainWave = std::sin((1.0f - lifeGainRate) * kPi * 2.0f) * lifeGainRate;
    const float lifeCountScaleX = 1.0f + lifeGainWave * 0.28f + lifeGainRate * 0.06f;
    const float lifeCountScaleY = 1.0f - lifeGainWave * 0.18f + lifeGainRate * 0.03f;
    const float coinPulseRate = hudCoinPulseTimer_ > 0.0f ? hudCoinPulseTimer_ / 0.24f : 0.0f;
    const float coinPulseScale = 1.0f + std::sin((1.0f - coinPulseRate) * kPi) * coinPulseRate * 0.12f;
    const Vector2 meterCenter = hudLifeMeter_.sprite ? hudLifeMeter_.basePosition : Vector2{ static_cast<float>(WinApp::kClientWidth) - 118.0f, 92.0f };

    if (hudLifeMeter_.sprite) {
        const uint32_t handle = Sprite::LoadTexture("ui/hud/life_meter_" + std::to_string(lifeValue) + ".png");
        hudLifeMeter_.sprite->SetTextureHandle(handle);
        hudLifeMeter_.sprite->SetVisible(visible);
        hudLifeMeter_.sprite->SetPosition(meterCenter);
        hudLifeMeter_.sprite->SetSize({ hudLifeMeter_.baseSize.x * lifePulse, hudLifeMeter_.baseSize.y * lifePulse });
        hudLifeMeter_.sprite->SetRotation(pulse * 0.03f);
        hudLifeMeter_.sprite->SetColor({ hudLifeMeter_.baseColor.x, hudLifeMeter_.baseColor.y, hudLifeMeter_.baseColor.z, visible ? hudLifeMeter_.baseColor.w : 0.0f });
        hudLifeMeter_.sprite->Update();
    }
    if (hudLifeMeterDigit_.sprite) {
        const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(lifeValue) + ".png");
        hudLifeMeterDigit_.sprite->SetTextureHandle(handle);
        hudLifeMeterDigit_.sprite->SetVisible(visible);
        hudLifeMeterDigit_.sprite->SetPosition(hudLifeMeterDigit_.basePosition);
        hudLifeMeterDigit_.sprite->SetSize({ hudLifeMeterDigit_.baseSize.x * lifePulse, hudLifeMeterDigit_.baseSize.y * lifePulse });
        hudLifeMeterDigit_.sprite->SetColor(lifeValue <= 1 ? Vector4{ 1.0f, 0.35f, 0.25f, 1.0f } : hudLifeMeterDigit_.baseColor);
        hudLifeMeterDigit_.sprite->Update();
    }
    if (hudLifeIcon_.sprite) {
        hudLifeIcon_.sprite->SetVisible(visible);
        hudLifeIcon_.sprite->SetPosition(hudLifeIcon_.basePosition);
        hudLifeIcon_.sprite->SetSize({ hudLifeIcon_.baseSize.x * lifeCountScaleX, hudLifeIcon_.baseSize.y * lifeCountScaleY });
        hudLifeIcon_.sprite->SetColor({ hudLifeIcon_.baseColor.x, hudLifeIcon_.baseColor.y, hudLifeIcon_.baseColor.z, visible ? hudLifeIcon_.baseColor.w : 0.0f });
        hudLifeIcon_.sprite->Update();
    }
    if (hudLifeXIcon_.sprite) {
        hudLifeXIcon_.sprite->SetVisible(visible);
        hudLifeXIcon_.sprite->SetPosition(hudLifeXIcon_.basePosition);
        hudLifeXIcon_.sprite->SetSize({ hudLifeXIcon_.baseSize.x * (1.0f + lifeGainRate * 0.08f), hudLifeXIcon_.baseSize.y * (1.0f + lifeGainRate * 0.08f) });
        hudLifeXIcon_.sprite->SetColor({ hudLifeXIcon_.baseColor.x, hudLifeXIcon_.baseColor.y, hudLifeXIcon_.baseColor.z, visible ? hudLifeXIcon_.baseColor.w : 0.0f });
        hudLifeXIcon_.sprite->Update();
    }

    const Vector2 lifeNumberRight = hudLifeDigits_[1].sprite ? hudLifeDigits_[1].basePosition : Vector2{ 162.0f, 100.0f };
    const float lifeDigitHeight = hudLifeDigits_[1].baseSize.y > 0.0f ? hudLifeDigits_[1].baseSize.y : 40.0f;
    SetGameplayHUDNumber(
        hudLifeDigits_,
        lives,
        lifeNumberRight,
        lifeDigitHeight * (1.0f + lifeGainRate * 0.12f),
        hudLifeDigits_[1].baseColor,
        visible
    );

    if (hudCoinIcon_.sprite) {
        hudCoinIcon_.sprite->SetVisible(visible);
        hudCoinIcon_.sprite->SetPosition(hudCoinIcon_.basePosition);
        hudCoinIcon_.sprite->SetSize({ hudCoinIcon_.baseSize.x * coinPulseScale, hudCoinIcon_.baseSize.y * coinPulseScale });
        hudCoinIcon_.sprite->SetColor({ hudCoinIcon_.baseColor.x, hudCoinIcon_.baseColor.y, hudCoinIcon_.baseColor.z, visible ? hudCoinIcon_.baseColor.w : 0.0f });
        hudCoinIcon_.sprite->Update();
    }
    if (hudCoinXIcon_.sprite) {
        hudCoinXIcon_.sprite->SetVisible(visible);
        hudCoinXIcon_.sprite->SetPosition(hudCoinXIcon_.basePosition);
        hudCoinXIcon_.sprite->SetSize({ hudCoinXIcon_.baseSize.x * coinPulseScale, hudCoinXIcon_.baseSize.y * coinPulseScale });
        hudCoinXIcon_.sprite->SetColor({ hudCoinXIcon_.baseColor.x, hudCoinXIcon_.baseColor.y, hudCoinXIcon_.baseColor.z, visible ? hudCoinXIcon_.baseColor.w : 0.0f });
        hudCoinXIcon_.sprite->Update();
    }

    const Vector2 coinNumberRight = hudCoinDigits_[1].sprite ? hudCoinDigits_[1].basePosition : Vector2{ 162.0f, 154.0f };
    const float coinDigitHeight = hudCoinDigits_[1].baseSize.y > 0.0f ? hudCoinDigits_[1].baseSize.y : 40.0f;
    SetGameplayHUDNumber(
        hudCoinDigits_,
        coins,
        coinNumberRight,
        coinDigitHeight * coinPulseScale,
        hudCoinDigits_[1].baseColor,
        visible
    );
    UpdateStageStarHUD(deltaTime, visible);
}

void GamePlayScene::StartLifeLostPresentation(int beforeLives, int afterLives) {
    lifeLostPresentationActive_ = true;
    lifeLostPresentationFinished_ = false;
    lifeLostBlackHold_ = true;
    lifeLostNumberDropped_ = false;
    lifeLostRevive_ = afterLives > 0;
    lifeLostPresentationTimer_ = 0.0f;
    lifeLostBeforeLives_ = std::clamp(beforeLives, 0, 99);
    lifeLostAfterLives_ = std::clamp(afterLives, 0, 99);
    lifeLostIrisCenter_ = { 0.5f, 0.5f };
    if (lifeLostSlimeObject_) {
        lifeLostSlimeObject_->SetIsVisible(true);
    }
    if (lifeLostStunObject_) {
        lifeLostStunObject_->SetIsVisible(true);
    }
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
    if (lifeLostCamera_) {
        CameraManager::GetInstance()->SetActiveCamera(lifeLostCamera_.get());
    }
}

void GamePlayScene::HideLifeLostPresentationOverlay() {
    lifeLostPresentationActive_ = false;
    lifeLostPresentationFinished_ = true;
    lifeLostBlackHold_ = false;
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    if (lifeLostIcon_.sprite) lifeLostIcon_.sprite->SetVisible(false);
    if (lifeLostXIcon_.sprite) lifeLostXIcon_.sprite->SetVisible(false);
    if (lifeLostBackdrop_.sprite) lifeLostBackdrop_.sprite->SetVisible(false);
    if (lifeLostSlimeObject_) lifeLostSlimeObject_->SetIsVisible(false);
    if (lifeLostStunObject_) lifeLostStunObject_->SetIsVisible(false);
    for (auto& digit : lifeLostDigits_) {
        if (digit.sprite) digit.sprite->SetVisible(false);
    }
}

void GamePlayScene::UpdateLifeLostPresentation(float deltaTime) {
    if (!lifeLostPresentationActive_) {
        if (lifeLostBlackHold_) {
            const float screenW = static_cast<float>(WinApp::kClientWidth);
            const float screenH = static_cast<float>(WinApp::kClientHeight);
            if (lifeLostBackdrop_.sprite) {
                lifeLostBackdrop_.sprite->SetVisible(true);
                lifeLostBackdrop_.sprite->SetPosition({ screenW * 0.5f, screenH * 0.5f });
                lifeLostBackdrop_.sprite->SetSize({ screenW, screenH });
                lifeLostBackdrop_.sprite->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
                lifeLostBackdrop_.sprite->Update();
            }
            if (lifeLostIcon_.sprite) lifeLostIcon_.sprite->SetVisible(false);
            if (lifeLostXIcon_.sprite) lifeLostXIcon_.sprite->SetVisible(false);
            for (auto& digit : lifeLostDigits_) {
                if (digit.sprite) digit.sprite->SetVisible(false);
            }
            return;
        }
        if (lifeLostIcon_.sprite) lifeLostIcon_.sprite->SetVisible(false);
        if (lifeLostXIcon_.sprite) lifeLostXIcon_.sprite->SetVisible(false);
        if (lifeLostBackdrop_.sprite) lifeLostBackdrop_.sprite->SetVisible(false);
        for (auto& digit : lifeLostDigits_) {
            if (digit.sprite) digit.sprite->SetVisible(false);
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
        lifeLostPresentationActive_ = false;
        lifeLostPresentationFinished_ = true;
        lifeLostBlackHold_ = true;
        if (lifeLostIcon_.sprite) lifeLostIcon_.sprite->SetVisible(false);
        if (lifeLostXIcon_.sprite) lifeLostXIcon_.sprite->SetVisible(false);
        if (lifeLostSlimeObject_) lifeLostSlimeObject_->SetIsVisible(false);
        if (lifeLostStunObject_) lifeLostStunObject_->SetIsVisible(false);
        for (auto& digit : lifeLostDigits_) {
            if (digit.sprite) digit.sprite->SetVisible(false);
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

    if (lifeLostBackdrop_.sprite) {
        lifeLostBackdrop_.sprite->SetVisible(true);
        lifeLostBackdrop_.sprite->SetPosition({ centerX, centerY });
        lifeLostBackdrop_.sprite->SetSize({ screenW, screenH });
        lifeLostBackdrop_.sprite->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        lifeLostBackdrop_.sprite->Update();
    }
    if (lifeLostIcon_.sprite) {
        lifeLostIcon_.sprite->SetVisible(true);
        lifeLostIcon_.sprite->SetPosition(lifeLostIcon_.basePosition);
        lifeLostIcon_.sprite->SetSize({ lifeLostIcon_.baseSize.x * settlePulse, lifeLostIcon_.baseSize.y * settlePulse });
        lifeLostIcon_.sprite->SetColor({ lifeLostIcon_.baseColor.x, lifeLostIcon_.baseColor.y, lifeLostIcon_.baseColor.z, alpha });
        lifeLostIcon_.sprite->Update();
    }
    if (lifeLostXIcon_.sprite) {
        lifeLostXIcon_.sprite->SetVisible(true);
        lifeLostXIcon_.sprite->SetPosition(lifeLostXIcon_.basePosition);
        lifeLostXIcon_.sprite->SetSize(lifeLostXIcon_.baseSize);
        lifeLostXIcon_.sprite->SetColor({ lifeLostXIcon_.baseColor.x, lifeLostXIcon_.baseColor.y, lifeLostXIcon_.baseColor.z, alpha });
        lifeLostXIcon_.sprite->Update();
    }

    const int tens = displayLives / 10;
    const int ones = displayLives % 10;
    const bool showTens = displayLives >= 10;
    const float digitHeight = (lifeLostDigits_[1].baseSize.y > 0.0f ? lifeLostDigits_[1].baseSize.y : 86.0f) * settlePulse;
    const float digitWidth = digitHeight * 0.68f;
    const float spacing = digitWidth * 0.82f;
    const float rightX = lifeLostDigits_[1].sprite ? lifeLostDigits_[1].basePosition.x : centerX + 132.0f;
    const float digitY = lifeLostDigits_[1].sprite ? lifeLostDigits_[1].basePosition.y : centerY + 2.0f;
    const float totalWidth = showTens ? spacing + digitWidth : digitWidth;
    const float startX = rightX - totalWidth + digitWidth * 0.5f;
    const std::array<int, 2> values = { tens, ones };

    for (int i = 0; i < 2; ++i) {
        Sprite* digit = lifeLostDigits_[i].sprite;
        if (!digit) continue;

        const bool digitVisible = showTens || i == 1;
        digit->SetVisible(digitVisible);
        if (!digitVisible) continue;

        const int sourceIndex = showTens ? i : 1;
        const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(values[sourceIndex]) + ".png");
        digit->SetTextureHandle(handle);
        digit->SetPosition({ startX + (sourceIndex - (2 - (showTens ? 2 : 1))) * spacing, digitY });
        digit->SetSize({ digitWidth, digitHeight });
        digit->SetColor(lifeLostNumberDropped_
            ? Vector4{ 1.0f, 0.65f, 0.22f, alpha }
            : Vector4{ 1.0f, 0.92f, 0.38f, alpha });
        digit->Update();
    }
}

void GamePlayScene::DrawGameplayHUD() {
    if (lifeLostPresentationActive_ || lifeLostBlackHold_) {
        DrawLifeLostPresentation();
        return;
    }

    DrawGameplayHUDSprite(hudLifeIcon_);
    DrawGameplayHUDSprite(hudLifeXIcon_);
    for (auto& digit : hudLifeDigits_) {
        DrawGameplayHUDSprite(digit);
    }
    DrawGameplayHUDSprite(hudCoinIcon_);
    DrawGameplayHUDSprite(hudCoinXIcon_);
    for (auto& digit : hudCoinDigits_) {
        DrawGameplayHUDSprite(digit);
    }
    DrawStageStarHUD();
    DrawGameplayHUDSprite(hudLifeMeter_);
    DrawGameplayHUDSprite(hudLifeMeterDigit_);
    DrawLifeLostPresentation();
}

void GamePlayScene::DrawStageStarHUD() {
    for (const auto& slot : hudStageStarSlots_) {
        DrawGameplayHUDSprite(slot);
    }
    for (const auto& particle : hudStageStarFlyParticles_) {
        if (particle.sprite) {
            particle.sprite->Draw();
        }
    }
}

void GamePlayScene::DrawLifeLostPresentation() {
    if (!lifeLostPresentationActive_ && !lifeLostBlackHold_) return;
    if (!lifeLostPresentationActive_) {
        DrawGameplayHUDSprite(lifeLostBackdrop_);
    }
    if (!lifeLostPresentationActive_) return;
    DrawGameplayHUDSprite(lifeLostIcon_);
    DrawGameplayHUDSprite(lifeLostXIcon_);
    for (auto& digit : lifeLostDigits_) {
        DrawGameplayHUDSprite(digit);
    }
}

void GamePlayScene::InitializeLifeLostPresentationObjects() {
    if (!object3dCommon_) {
        return;
    }

    lifeLostCamera_ = std::make_unique<Camera>();
    lifeLostCamera_->Initialize();
    lifeLostCamera_->SetInputEnabled(false);
    lifeLostCamera_->SetFollowTarget(nullptr);
    lifeLostCamera_->SetFollowMode(Camera::FollowMode::kFixedPoint);
    lifeLostCamera_->SetFovY(0.48f);
    lifeLostCamera_->ConfigFixedPoint({ 0.0f, 2.25f, -7.2f }, { 0.10f, 0.0f, 0.0f });
    lifeLostCamera_->Update();

    lifeLostSlimeObject_ = std::make_unique<Object3d>();
    lifeLostSlimeObject_->Initialize(object3dCommon_.get());
    lifeLostSlimeObject_->SetName("LifeLostSlime");
    lifeLostSlimeObject_->SetModel("Characters/slime");
    lifeLostSlimeObject_->SetBlendMode(BlendMode::kNormal);
    lifeLostSlimeObject_->SetEnableEnvMap(false);
    lifeLostSlimeObject_->SetEmissive(1.22f);
    lifeLostSlimeObject_->SetColor({ 0.38f, 0.92f, 1.0f, 1.0f });
    lifeLostSlimeObject_->SetIsVisible(false);

    lifeLostStunObject_ = std::make_unique<Object3d>();
    lifeLostStunObject_->Initialize(object3dCommon_.get());
    lifeLostStunObject_->SetName("LifeLostStun");
    lifeLostStunObject_->SetModel("Primitives/sphere");
    lifeLostStunObject_->SetMaterialType(18);
    lifeLostStunObject_->SetBlendMode(BlendMode::kNormal);
    lifeLostStunObject_->SetColor({ 1.0f, 0.94f, 0.28f, 0.78f });
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
    (void)deltaTime;
    const float t = lifeLostPresentationTimer_;

    if (lifeLostCamera_) {
        CameraManager::GetInstance()->SetActiveCamera(lifeLostCamera_.get());
        lifeLostCamera_->ConfigFixedPoint({ 0.0f, 2.25f + std::sin(t * 0.9f) * 0.04f, -7.2f }, { 0.10f, 0.0f, 0.0f });
        lifeLostCamera_->Update();
    }

    const float fadeIn = SmoothStep(t / 0.38f);
    const float standT = lifeLostRevive_ ? SmoothStep((t - 1.72f) / 0.62f) : 0.0f;
    const float jumpT = lifeLostRevive_ ? std::clamp((t - 2.36f) / 0.58f, 0.0f, 1.0f) : 0.0f;
    const float jumpArc = std::sin(jumpT * kPi) * (1.0f - jumpT * 0.15f);
    const float wobble = std::sin(t * 18.0f) * (1.0f - standT) * 0.08f;
    const float reviveSquash = lifeLostRevive_ ? std::sin(jumpT * kPi * 2.0f) * (1.0f - jumpT) * 0.14f : 0.0f;

    if (lifeLostSlimeObject_) {
        const Vector3 scale = {
            (1.70f + wobble + reviveSquash) * fadeIn,
            (0.58f + standT * 0.52f - reviveSquash * 0.8f) * fadeIn,
            (1.70f - wobble + reviveSquash) * fadeIn
        };
        const Vector3 rotate = {
            (1.0f - standT) * kPi,
            std::sin(t * 1.6f) * 0.10f * (1.0f - standT),
            std::sin(t * 4.0f) * 0.22f * (1.0f - standT)
        };
        lifeLostSlimeObject_->SetIsVisible(fadeIn > 0.01f);
        lifeLostSlimeObject_->SetTranslate({ 0.0f, 0.44f + jumpArc * 1.28f + standT * 0.08f, 0.0f });
        lifeLostSlimeObject_->SetRotation(rotate);
        lifeLostSlimeObject_->SetScale(scale);
        lifeLostSlimeObject_->Update(1.0f / 60.0f);
    }

    if (lifeLostStunObject_) {
        const float stunFade = lifeLostRevive_
            ? (1.0f - SmoothStep((t - 1.45f) / 0.48f))
            : 1.0f;
        lifeLostStunObject_->SetIsVisible(stunFade > 0.04f && fadeIn > 0.05f);
        lifeLostStunObject_->SetTranslate({ 0.0f, 1.58f + std::sin(t * 5.8f) * 0.06f, 0.0f });
        lifeLostStunObject_->SetRotation({ std::sin(t * 2.1f) * 0.18f, t * 3.5f, t * 1.4f });
        const float stunScale = (2.05f + std::sin(t * 6.0f) * 0.07f) * fadeIn;
        lifeLostStunObject_->SetScale({ stunScale, stunScale, stunScale });
        lifeLostStunObject_->SetColor({ 1.0f, 0.94f, 0.28f, 0.70f * stunFade * fadeIn });
        if (lifeLostStunObject_->GetMeshRenderer() && lifeLostStunObject_->GetMeshRenderer()->GetWaterParamData()) {
            auto* stun = lifeLostStunObject_->GetMeshRenderer()->GetWaterParamData();
            stun->effectIntensity = 0.42f * stunFade + 0.10f;
            stun->billboardScale = 1.02f + std::sin(t * 4.0f) * 0.05f;
        }
        lifeLostStunObject_->Update(1.0f / 60.0f);
    }
}

void GamePlayScene::DrawLifeLostPresentationWorld(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
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

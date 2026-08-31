#define NOMINMAX
#include "GamePlayScene.h"

#include "CameraManager.h"
#include "EnemyFalseKingSlime.h"
#include "EnemyMagmaSlime.h"
#include "EnemyPrismSlime.h"
#include "GameDataManager.h"
#include "LevelLoader.h"
#include "Sprite.h"
#include "SpriteLayoutScaler.h"
#include "StageManager.h"
#include "TextureManager.h"
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

void SetFullTextureRect(Sprite* sprite, uint32_t textureHandle) {
    if (!sprite || textureHandle == 0) {
        return;
    }

    sprite->SetTextureHandle(textureHandle);
    const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle);
    sprite->SetTextureRect(
        { 0.0f, 0.0f },
        { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
}

const char* ResolveHpIconTexture(const Player* player, bool showHurtIcon) {
    if (player && player->IsEnemyMorphed()) {
        switch (player->GetEnemyMorphType()) {
        case Player::EnemyMorphType::Slime:
            return showHurtIcon ? "ui/portraits/slime_hurt.png" : "ui/portraits/slime.png";
        case Player::EnemyMorphType::Bomber:
            return showHurtIcon ? "ui/portraits/bomber_hurt.png" : "ui/portraits/bomber.png";
        case Player::EnemyMorphType::Bat:
            return showHurtIcon ? "ui/portraits/bat_hurt.png" : "ui/portraits/bat.png";
        case Player::EnemyMorphType::BeamDrone:
            return showHurtIcon ? "ui/portraits/beam_drone_hurt.png" : "ui/portraits/beam_drone.png";
        case Player::EnemyMorphType::Mushroom:
            return showHurtIcon ? "ui/portraits/mushroom_hurt.png" : "ui/portraits/mushroom.png";
        case Player::EnemyMorphType::FireSlime:
            return showHurtIcon ? "ui/portraits/fire_slime_hurt.png" : "ui/portraits/fire_slime.png";
        case Player::EnemyMorphType::ThunderSlime:
            return showHurtIcon ? "ui/portraits/thunder_slime_hurt.png" : "ui/portraits/thunder_slime.png";
        case Player::EnemyMorphType::WindSlime:
            return showHurtIcon ? "ui/portraits/wind_slime_hurt.png" : "ui/portraits/wind_slime.png";
        default:
            break;
        }
    }

    return showHurtIcon ? "ui/portraits/player_hurt.png" : "ui/portraits/player.png";
}
}

void GamePlayScene::UpdateUI(float deltaTime) {
    UpdateGameplayHUD(deltaTime);
    UpdateLifeLostPresentation(deltaTime);
    if (saveIndicatorOverlay_) {
        saveIndicatorOverlay_->Update(deltaTime);
    }
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
        sprite->SetPosition(SpriteLayoutScaler::ScaleDesignPosition(position));
        sprite->SetSize(SpriteLayoutScaler::ScaleDesignSize(size));
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

    hudHpIcon_ = BindGameplayHUDSprite(
        "hud_hp_icon",
        "Resources/sprite/ui/portraits/player.png",
        { 116.0f, 988.0f },
        { 78.0f, 78.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 1.0f }
    );
    hudHpDamageFill_ = BindGameplayHUDSprite(
        "hud_hp_damage_fill",
        "Resources/sprite/ui/hud/hp_damage_fill.png",
        { 162.0f, 988.0f },
        { 298.0f, 16.0f },
        { 0.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.92f }
    );
    hudHpFill_ = BindGameplayHUDSprite(
        "hud_hp_fill",
        "Resources/sprite/ui/hud/hp_fill.png",
        { 162.0f, 988.0f },
        { 298.0f, 16.0f },
        { 0.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 1.0f }
    );
    if (hudHpFill_.sprite) {
        hudHpFill_.sprite->SetAnimation(8, 0.08f, true);
        hudHpFill_.sprite->Play();
        hudHpFill_.sprite->SetSize(hudHpFill_.baseSize);
        hudHpFill_.sprite->Update();
    }
    hudHpHighlight_ = BindGameplayHUDSprite(
        "hud_hp_highlight",
        "Resources/sprite/ui/hud/hp_highlight.png",
        { 162.0f, 988.0f },
        { 298.0f, 16.0f },
        { 0.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.38f }
    );
    if (hudHpHighlight_.sprite) {
        hudHpHighlight_.sprite->SetAnimation(6, 0.10f, true);
        hudHpHighlight_.sprite->Play();
        hudHpHighlight_.sprite->SetSize(hudHpHighlight_.baseSize);
        hudHpHighlight_.sprite->Update();
    }
    hudHpFrame_ = BindGameplayHUDSprite(
        "hud_hp_frame",
        "Resources/sprite/ui/hud/hp_bar_frame.png",
        { 72.0f, 988.0f },
        { 410.0f, 88.0f },
        { 0.0f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.98f }
    );
    hudMorphGaugeBack_ = BindGameplayHUDSprite(
        "hud_morph_gauge_back",
        "Resources/sprite/ui/hud/morph_gauge/back.png",
        { 1456.0f, 194.0f },
        { 112.0f, 112.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.0f }
    );
    hudMorphGaugeFill_ = BindGameplayHUDSprite(
        "hud_morph_gauge_fill",
        "Resources/sprite/ui/hud/morph_gauge/fill_32.png",
        { 1456.0f, 194.0f },
        { 112.0f, 112.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.0f }
    );
    hudMorphGaugeIcon_ = BindGameplayHUDSprite(
        "hud_morph_gauge_icon",
        "Resources/sprite/ui/hud/morph_gauge/icon.png",
        { 1456.0f, 194.0f },
        { 78.0f, 78.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.0f }
    );
    hudMorphGaugeFrame_ = BindGameplayHUDSprite(
        "hud_morph_gauge_frame",
        "Resources/sprite/ui/hud/morph_gauge/frame.png",
        { 1456.0f, 194.0f },
        { 112.0f, 112.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 0.0f }
    );
    prismBossHudGlow_ = BindGameplayHUDSprite(
        "hud_boss_prism_glow",
        "Resources/sprite/common/white.png",
        { 960.0f, 152.0f },
        { 800.0f, 54.0f },
        { 0.5f, 0.5f },
        { 0.36f, 0.64f, 1.0f, 0.34f }
    );
    prismBossHudBack_ = BindGameplayHUDSprite(
        "hud_boss_prism_back",
        "Resources/sprite/common/white.png",
        { 960.0f, 152.0f },
        { 770.0f, 40.0f },
        { 0.5f, 0.5f },
        { 0.015f, 0.012f, 0.055f, 0.94f }
    );
    prismBossHudTrack_ = BindGameplayHUDSprite(
        "hud_boss_prism_track",
        "Resources/sprite/common/white.png",
        { 960.0f, 152.0f },
        { 730.0f, 20.0f },
        { 0.5f, 0.5f },
        { 0.025f, 0.055f, 0.12f, 0.98f }
    );
    prismBossHudDamageFill_ = BindGameplayHUDSprite(
        "hud_boss_prism_damage_fill",
        "Resources/sprite/common/white.png",
        { 595.0f, 152.0f },
        { 730.0f, 18.0f },
        { 0.0f, 0.5f },
        { 1.0f, 0.28f, 0.90f, 0.90f }
    );
    prismBossHudFill_ = BindGameplayHUDSprite(
        "hud_boss_prism_fill",
        "Resources/sprite/common/white.png",
        { 595.0f, 152.0f },
        { 730.0f, 18.0f },
        { 0.0f, 0.5f },
        { 0.18f, 0.86f, 1.0f, 1.0f }
    );
    prismBossHudHighlight_ = BindGameplayHUDSprite(
        "hud_boss_prism_highlight",
        "Resources/sprite/common/white.png",
        { 595.0f, 148.0f },
        { 730.0f, 5.0f },
        { 0.0f, 0.5f },
        { 0.88f, 1.0f, 1.0f, 0.70f }
    );
    prismBossHudFrameTop_ = BindGameplayHUDSprite(
        "hud_boss_prism_frame_top",
        "Resources/sprite/common/white.png",
        { 960.0f, 139.0f },
        { 770.0f, 4.0f },
        { 0.5f, 0.5f },
        { 0.58f, 0.94f, 1.0f, 1.0f }
    );
    prismBossHudFrameBottom_ = BindGameplayHUDSprite(
        "hud_boss_prism_frame_bottom",
        "Resources/sprite/common/white.png",
        { 960.0f, 165.0f },
        { 770.0f, 4.0f },
        { 0.5f, 0.5f },
        { 0.66f, 0.48f, 1.0f, 1.0f }
    );
    prismBossHudShardLeft_ = BindGameplayHUDSprite(
        "hud_boss_prism_shard_left",
        "Resources/sprite/particle/diamond_shard.png",
        { 566.0f, 152.0f },
        { 68.0f, 80.0f },
        { 0.5f, 0.5f },
        { 0.48f, 0.92f, 1.0f, 0.96f }
    );
    prismBossHudShardRight_ = BindGameplayHUDSprite(
        "hud_boss_prism_shard_right",
        "Resources/sprite/particle/diamond_shard.png",
        { 1354.0f, 152.0f },
        { 68.0f, 80.0f },
        { 0.5f, 0.5f },
        { 0.78f, 0.48f, 1.0f, 0.96f }
    );
    prismBossHudName_ = BindGameplayHUDSprite(
        "hud_boss_prism_name",
        "Resources/sprite/ui/hud/boss/prism_slime_name.png",
        { 960.0f, 84.0f },
        { 400.0f, 84.0f },
        { 0.5f, 0.5f },
        { 1.0f, 1.0f, 1.0f, 1.0f }
    );
    const std::array<Vector2, 6> bossGlintPositions = {
        Vector2{ 742.0f, 76.0f },
        Vector2{ 1178.0f, 76.0f },
        Vector2{ 568.0f, 132.0f },
        Vector2{ 1352.0f, 132.0f },
        Vector2{ 960.0f, 118.0f },
        Vector2{ 595.0f, 152.0f },
    };
    for (size_t i = 0; i < prismBossHudGlints_.size(); ++i) {
        prismBossHudGlints_[i] = BindGameplayHUDSprite(
            "hud_boss_prism_glint_" + std::to_string(i),
            "Resources/sprite/particle/spark_star.png",
            bossGlintPositions[i],
            { i == 4 ? 24.0f : 20.0f, i == 4 ? 24.0f : 20.0f },
            { 0.5f, 0.5f },
            { 0.82f, 0.98f, 1.0f, 1.0f }
        );
    }
    const std::array<Vector2, 3> falseKingPhaseCrownPositions = {
        Vector2{ 716.0f, 194.0f },
        Vector2{ 960.0f, 194.0f },
        Vector2{ 1204.0f, 194.0f },
    };
    for (size_t i = 0; i < falseKingBossHudPhaseCrowns_.size(); ++i) {
        falseKingBossHudPhaseCrowns_[i] = BindGameplayHUDSprite(
            "hud_boss_false_king_phase_crown_" + std::to_string(i),
            "Resources/sprite/ui/title/crown_progress_icon.png",
            falseKingPhaseCrownPositions[i],
            { 38.0f, 38.0f },
            { 0.5f, 0.5f },
            { 1.0f, 0.78f, 0.20f, 0.0f }
        );
    }
    const std::array<Vector2, 2> falseKingPhaseDividerPositions = {
        Vector2{ 838.0f, 152.0f },
        Vector2{ 1082.0f, 152.0f },
    };
    for (size_t i = 0; i < falseKingBossHudPhaseDividers_.size(); ++i) {
        falseKingBossHudPhaseDividers_[i] = BindGameplayHUDSprite(
            "hud_boss_false_king_phase_divider_" + std::to_string(i),
            "Resources/sprite/common/white.png",
            falseKingPhaseDividerPositions[i],
            { 5.0f, 28.0f },
            { 0.5f, 0.5f },
            { 1.0f, 0.84f, 0.28f, 0.0f }
        );
    }
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
    {
        const float maxHp = player_ ? std::max(player_->GetMaxHp(), 1.0f) : 1.0f;
        hudHpDelayedRate_ = player_ ? std::clamp(hudPreviousHp_ / maxHp, 0.0f, 1.0f) : 0.0f;
    }
    hudDamagePulseTimer_ = 0.0f;
    hudHurtIconTimer_ = 0.0f;
    hudHpDamageHoldTimer_ = 0.0f;
    hudHpAnimationTimer_ = 0.0f;
    hudMorphGaugeTimer_ = 0.0f;
    hudMorphGaugeVisibleTimer_ = 0.0f;
    prismBossHudPhase_ = PrismBossHudPhase::Hidden;
    prismBossHudTimer_ = 0.0f;
    prismBossHudDisplayedRate_ = 0.0f;
    prismBossHudDelayedRate_ = 0.0f;
    prismBossHudPreviousHp_ = 0.0f;
    prismBossHudDamagePulseTimer_ = 0.0f;
    prismBossHudDamageHoldTimer_ = 0.0f;
    prismBossHudAnimationTimer_ = 0.0f;
    prismBossHudTheme_ = BossHudTheme::Prism;
    falseKingBossHudPreviousPhase_ = 1;
    falseKingBossHudPhasePulseTimer_ = 0.0f;
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
    const float hpRate = std::clamp(hp / maxHp, 0.0f, 1.0f);
    const float previousHpRate = std::clamp(hudPreviousHp_ / maxHp, 0.0f, 1.0f);
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

    const bool tookDamage = visible && hp < hudPreviousHp_ - 0.01f;
    if (tookDamage) {
        hudDamagePulseTimer_ = 0.42f;
        hudHurtIconTimer_ = 0.48f;
        hudHpDamageHoldTimer_ = 0.18f;
        hudHpDelayedRate_ = std::max(hudHpDelayedRate_, previousHpRate);
    }
    hudPreviousHp_ = hp;
    hudDamagePulseTimer_ = std::max(0.0f, hudDamagePulseTimer_ - deltaTime);
    hudHurtIconTimer_ = std::max(0.0f, hudHurtIconTimer_ - deltaTime);
    if (!visible) {
        hudHpDelayedRate_ = 0.0f;
        hudHpDamageHoldTimer_ = 0.0f;
    } else if (hpRate >= hudHpDelayedRate_) {
        hudHpDelayedRate_ = hpRate;
        hudHpDamageHoldTimer_ = 0.0f;
    } else if (hudHpDamageHoldTimer_ > 0.0f) {
        hudHpDamageHoldTimer_ = std::max(0.0f, hudHpDamageHoldTimer_ - deltaTime);
    } else {
        hudHpDelayedRate_ = std::max(hpRate, hudHpDelayedRate_ - deltaTime * 0.62f);
    }
    hudLifeGainPulseTimer_ = std::max(0.0f, hudLifeGainPulseTimer_ - deltaTime);
    hudCoinPulseTimer_ = std::max(0.0f, hudCoinPulseTimer_ - deltaTime);
    hudHpAnimationTimer_ = std::fmod(hudHpAnimationTimer_ + std::max(deltaTime, 0.0f), 1000.0f);

    const float pulse = hudDamagePulseTimer_ > 0.0f ? std::sin(hudDamagePulseTimer_ * 70.0f) : 0.0f;
    const float damageRate = std::clamp(hudDamagePulseTimer_ / 0.42f, 0.0f, 1.0f);
    const float damageImpact = std::sin((1.0f - damageRate) * kPi) * damageRate;
    const float lowHpRate = visible ? std::clamp((0.34f - hpRate) / 0.34f, 0.0f, 1.0f) : 0.0f;
    const float lowHpPulse = lowHpRate * (0.5f + 0.5f * std::sin(hudHpAnimationTimer_ * (5.0f + lowHpRate * 3.0f)));
    const float liquidWave = std::sin(hudHpAnimationTimer_ * 4.6f);
    const float idleIconPulse = 1.0f + std::sin(hudHpAnimationTimer_ * 2.2f) * 0.012f + lowHpPulse * 0.035f;
    const float hpIconPulse = idleIconPulse + std::abs(pulse) * damageRate * 0.07f;
    const float barShake = damageRate > 0.0f
        ? std::sin(hudDamagePulseTimer_ * 92.0f) * SpriteLayoutScaler::ScaleDesignX(3.2f) * damageRate
        : 0.0f;
    const Vector2 liquidOffset = { barShake, 0.0f };
    const float liquidHeightScale = 1.0f + damageImpact * 0.07f;
    const float lifeGainRate = hudLifeGainPulseTimer_ > 0.0f ? hudLifeGainPulseTimer_ / 0.62f : 0.0f;
    const float lifeGainWave = std::sin((1.0f - lifeGainRate) * kPi * 2.0f) * lifeGainRate;
    const float lifeCountScaleX = 1.0f + lifeGainWave * 0.28f + lifeGainRate * 0.06f;
    const float lifeCountScaleY = 1.0f - lifeGainWave * 0.18f + lifeGainRate * 0.03f;
    const float coinPulseRate = hudCoinPulseTimer_ > 0.0f ? hudCoinPulseTimer_ / 0.24f : 0.0f;
    const float coinPulsePhase = 1.0f - coinPulseRate;
    const float coinBounce = std::sin(coinPulsePhase * kPi) * coinPulseRate;
    const float coinPulseScaleX = 1.0f + coinBounce * 0.20f;
    const float coinPulseScaleY = 1.0f + coinBounce * 0.28f;
    const float coinCountScale = 1.0f + coinBounce * 0.14f;
    const float coinBounceOffsetY = -12.0f * coinBounce;

    const bool damageReacting = hudHurtIconTimer_ > 0.0f;
    const float hpIconShakeX = damageReacting ? std::sin(hudHurtIconTimer_ * 85.0f) * SpriteLayoutScaler::ScaleDesignX(3.0f) : 0.0f;
    const float hpIconShakeY = damageReacting ? std::sin(hudHurtIconTimer_ * 61.0f) * SpriteLayoutScaler::ScaleDesignY(1.2f) : 0.0f;
    const float hpIconRotation = damageReacting ? std::sin(hudHurtIconTimer_ * 55.0f) * 0.055f : liquidWave * lowHpRate * 0.012f;
    if (hudHpIcon_.sprite) {
        const uint32_t handle = Sprite::LoadTexture(ResolveHpIconTexture(player_, damageReacting));
        SetFullTextureRect(hudHpIcon_.sprite, handle);
        hudHpIcon_.sprite->SetVisible(visible);
        hudHpIcon_.sprite->SetPosition({ hudHpIcon_.basePosition.x + hpIconShakeX, hudHpIcon_.basePosition.y + hpIconShakeY });
        hudHpIcon_.sprite->SetSize({ hudHpIcon_.baseSize.x * hpIconPulse, hudHpIcon_.baseSize.y * hpIconPulse });
        hudHpIcon_.sprite->SetRotation(hpIconRotation);
        hudHpIcon_.sprite->SetColor({ hudHpIcon_.baseColor.x, hudHpIcon_.baseColor.y, hudHpIcon_.baseColor.z, visible ? hudHpIcon_.baseColor.w : 0.0f });
        hudHpIcon_.sprite->Update();
    }
    if (hudHpDamageFill_.sprite) {
        const float rate = std::clamp(hudHpDelayedRate_, 0.0f, 1.0f);
        hudHpDamageFill_.sprite->SetVisible(visible && rate > 0.001f);
        hudHpDamageFill_.sprite->SetPosition({ hudHpDamageFill_.basePosition.x + liquidOffset.x, hudHpDamageFill_.basePosition.y + liquidOffset.y });
        hudHpDamageFill_.sprite->SetSize({ hudHpDamageFill_.baseSize.x * rate, hudHpDamageFill_.baseSize.y * liquidHeightScale });
        hudHpDamageFill_.sprite->SetColor({ hudHpDamageFill_.baseColor.x, hudHpDamageFill_.baseColor.y, hudHpDamageFill_.baseColor.z, visible ? hudHpDamageFill_.baseColor.w * (0.88f + damageRate * 0.12f) : 0.0f });
        hudHpDamageFill_.sprite->Update();
    }
    if (hudHpFill_.sprite) {
        hudHpFill_.sprite->SetVisible(visible && hpRate > 0.001f);
        hudHpFill_.sprite->SetPosition({ hudHpFill_.basePosition.x + liquidOffset.x, hudHpFill_.basePosition.y + liquidOffset.y });
        hudHpFill_.sprite->SetSize({ hudHpFill_.baseSize.x * hpRate, hudHpFill_.baseSize.y * liquidHeightScale });
        hudHpFill_.sprite->SetColor({ 1.0f, 1.0f - lowHpPulse * 0.08f, 1.0f - lowHpPulse * 0.14f, visible ? hudHpFill_.baseColor.w : 0.0f });
        hudHpFill_.sprite->Update();
    }
    if (hudHpHighlight_.sprite) {
        const float shimmer = 0.78f + hpRate * 0.10f + damageImpact * 0.12f;
        const float alpha = visible && hpRate > 0.001f ? hudHpHighlight_.baseColor.w * shimmer : 0.0f;
        hudHpHighlight_.sprite->SetVisible(visible && hpRate > 0.001f);
        hudHpHighlight_.sprite->SetPosition({ hudHpHighlight_.basePosition.x + liquidOffset.x, hudHpHighlight_.basePosition.y - SpriteLayoutScaler::ScaleDesignY(0.35f) });
        hudHpHighlight_.sprite->SetSize({ hudHpHighlight_.baseSize.x * hpRate, hudHpHighlight_.baseSize.y * liquidHeightScale });
        hudHpHighlight_.sprite->SetColor({ hudHpHighlight_.baseColor.x, hudHpHighlight_.baseColor.y, hudHpHighlight_.baseColor.z, alpha });
        hudHpHighlight_.sprite->Update();
    }
    if (hudHpFrame_.sprite) {
        hudHpFrame_.sprite->SetVisible(visible);
        hudHpFrame_.sprite->SetPosition({ hudHpFrame_.basePosition.x + barShake, hudHpFrame_.basePosition.y });
        hudHpFrame_.sprite->SetSize({ hudHpFrame_.baseSize.x * (1.0f + damageImpact * 0.012f), hudHpFrame_.baseSize.y * (1.0f + damageImpact * 0.055f) });
        hudHpFrame_.sprite->SetColor({ 1.0f, 1.0f - lowHpPulse * 0.10f, 1.0f - lowHpPulse * 0.16f, visible ? hudHpFrame_.baseColor.w : 0.0f });
        hudHpFrame_.sprite->Update();
    }

    const bool morphActive = visible && player_ && player_->HasEnemyMorphTimeLimit();
    const bool suppressMorphGauge = visible && player_ && player_->IsEnemyMorphed() && !player_->HasEnemyMorphTimeLimit();
    if (morphActive) {
        hudMorphGaugeVisibleTimer_ = 0.20f;
    } else if (suppressMorphGauge) {
        hudMorphGaugeVisibleTimer_ = 0.0f;
    } else {
        hudMorphGaugeVisibleTimer_ = std::max(0.0f, hudMorphGaugeVisibleTimer_ - deltaTime);
    }
    const bool morphVisible = morphActive || hudMorphGaugeVisibleTimer_ > 0.0f;
    const float morphRate = morphActive ? player_->GetEnemyMorphRate() : 0.0f;
    hudMorphGaugeTimer_ = morphVisible ? hudMorphGaugeTimer_ + deltaTime : 0.0f;
    Vector2 morphGaugeSize = SpriteLayoutScaler::ScaleDesignSize({ 132.0f, 132.0f });
    Vector2 morphIconSize = SpriteLayoutScaler::ScaleDesignSize({ 92.0f, 92.0f });
    Vector2 morphGaugePosition = SpriteLayoutScaler::ScaleDesignPosition({ 530.0f, 988.0f });
    if (hudHpFrame_.sprite) {
        morphGaugePosition = {
            hudHpFrame_.basePosition.x + hudHpFrame_.baseSize.x + SpriteLayoutScaler::ScaleDesignX(66.0f),
            hudHpFrame_.basePosition.y
        };
    }
    if (player_) {
        Vector3 headWorld = player_->GetWorldPosition();
        headWorld.y += 2.55f;
        Vector3 bodyWorld = player_->GetWorldPosition();
        bodyWorld.y += 0.65f;

        const Vector2 headScreen = ProjectWorldToScreen(headWorld);
        const Vector2 bodyScreen = ProjectWorldToScreen(bodyWorld);
        const float bodyHeightOnScreen = std::abs(bodyScreen.y - headScreen.y);
        const float perspectiveScale = std::clamp(bodyHeightOnScreen / SpriteLayoutScaler::ScaleDesignY(96.0f), 0.72f, 1.18f);
        morphGaugeSize = { morphGaugeSize.x * perspectiveScale, morphGaugeSize.y * perspectiveScale };
        morphIconSize = { morphIconSize.x * perspectiveScale, morphIconSize.y * perspectiveScale };
        morphGaugePosition = {
            headScreen.x + morphGaugeSize.x * 0.44f,
            headScreen.y - morphGaugeSize.y * (0.10f + 0.03f * std::sin(hudMorphGaugeTimer_ * 5.2f))
        };
    }
    const float gaugeHalfX = morphGaugeSize.x * 0.55f;
    const float gaugeHalfY = morphGaugeSize.y * 0.55f;
    morphGaugePosition.x = std::clamp(morphGaugePosition.x, gaugeHalfX, static_cast<float>(WinApp::kClientWidth) - gaugeHalfX);
    morphGaugePosition.y = std::clamp(morphGaugePosition.y, gaugeHalfY, static_cast<float>(WinApp::kClientHeight) - gaugeHalfY);
    const int morphFrame = std::clamp(static_cast<int>(std::round(morphRate * 32.0f)), 0, 32);
    const float lowRate = morphVisible ? std::clamp((0.34f - morphRate) / 0.34f, 0.0f, 1.0f) : 0.0f;
    const float lowBlink = lowRate > 0.0f ? 0.5f + 0.5f * std::sin(hudMorphGaugeTimer_ * (18.0f + lowRate * 20.0f)) : 0.0f;
    const float warningPulse = lowRate * (0.08f + lowBlink * 0.20f);
    const Vector2 warningOffset = {
        lowRate * std::sin(hudMorphGaugeTimer_ * 42.0f) * 2.2f,
        lowRate * std::sin(hudMorphGaugeTimer_ * 31.0f) * 0.9f
    };
    int visibleMorphFrame = morphFrame;
    if (lowRate > 0.0f && lowBlink > 0.68f) {
        visibleMorphFrame = std::max(0, morphFrame - 1);
    }
    const auto updateMorphSprite = [&](HudSpriteState& state, const Vector2& baseSize, float alphaScale, float scale, float rotation, Vector2 offset, float colorBoost) {
        if (!state.sprite) {
            return;
        }
        const float alpha = morphVisible ? std::clamp(alphaScale, 0.0f, 1.0f) : 0.0f;
        state.sprite->SetVisible(morphVisible);
        state.sprite->SetPosition({ morphGaugePosition.x + offset.x, morphGaugePosition.y + offset.y });
        state.sprite->SetSize({ baseSize.x * scale, baseSize.y * scale });
        state.sprite->SetRotation(rotation);
        state.sprite->SetColor({
            std::clamp(state.baseColor.x + colorBoost, 0.0f, 1.0f),
            std::clamp(state.baseColor.y + colorBoost, 0.0f, 1.0f),
            std::clamp(state.baseColor.z + colorBoost, 0.0f, 1.0f),
            alpha
        });
        state.sprite->Update();
    };
    if (hudMorphGaugeFill_.sprite) {
        const uint32_t handle = Sprite::LoadTexture("ui/hud/morph_gauge/fill_" + (visibleMorphFrame < 10 ? std::string("0") : std::string()) + std::to_string(visibleMorphFrame) + ".png");
        hudMorphGaugeFill_.sprite->SetTextureHandle(handle);
    }
    const float gaugeScale = 1.0f + warningPulse;
    updateMorphSprite(hudMorphGaugeBack_, morphGaugeSize, 0.82f, gaugeScale, 0.0f, warningOffset, warningPulse * 0.25f);
    updateMorphSprite(hudMorphGaugeFill_, morphGaugeSize, 1.0f, gaugeScale, 0.0f, warningOffset, warningPulse * 0.85f);
    updateMorphSprite(hudMorphGaugeIcon_, morphIconSize, 0.95f + lowBlink * lowRate * 0.05f, 1.0f + warningPulse * 0.7f, std::sin(hudMorphGaugeTimer_ * 18.0f) * warningPulse * 0.9f, warningOffset, warningPulse * 0.45f);
    updateMorphSprite(hudMorphGaugeFrame_, morphGaugeSize, 1.0f, gaugeScale, morphVisible ? (1.0f - morphRate) * 0.10f + std::sin(hudMorphGaugeTimer_ * 20.0f) * warningPulse * 0.35f : 0.0f, warningOffset, warningPulse * 0.35f);

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
        hudCoinIcon_.sprite->SetPosition({ hudCoinIcon_.basePosition.x, hudCoinIcon_.basePosition.y + coinBounceOffsetY });
        hudCoinIcon_.sprite->SetSize({ hudCoinIcon_.baseSize.x * coinPulseScaleX, hudCoinIcon_.baseSize.y * coinPulseScaleY });
        hudCoinIcon_.sprite->SetColor({ hudCoinIcon_.baseColor.x, hudCoinIcon_.baseColor.y, hudCoinIcon_.baseColor.z, visible ? hudCoinIcon_.baseColor.w : 0.0f });
        hudCoinIcon_.sprite->Update();
    }
    if (hudCoinXIcon_.sprite) {
        hudCoinXIcon_.sprite->SetVisible(visible);
        hudCoinXIcon_.sprite->SetPosition({ hudCoinXIcon_.basePosition.x, hudCoinXIcon_.basePosition.y + coinBounceOffsetY * 0.55f });
        hudCoinXIcon_.sprite->SetSize({ hudCoinXIcon_.baseSize.x * coinCountScale, hudCoinXIcon_.baseSize.y * coinCountScale });
        hudCoinXIcon_.sprite->SetColor({ hudCoinXIcon_.baseColor.x, hudCoinXIcon_.baseColor.y, hudCoinXIcon_.baseColor.z, visible ? hudCoinXIcon_.baseColor.w : 0.0f });
        hudCoinXIcon_.sprite->Update();
    }

    const Vector2 coinNumberRight = hudCoinDigits_[1].sprite ? hudCoinDigits_[1].basePosition : Vector2{ 162.0f, 154.0f };
    const float coinDigitHeight = hudCoinDigits_[1].baseSize.y > 0.0f ? hudCoinDigits_[1].baseSize.y : 40.0f;
    SetGameplayHUDNumber(
        hudCoinDigits_,
        coins,
        { coinNumberRight.x, coinNumberRight.y + coinBounceOffsetY * 0.55f },
        coinDigitHeight * coinCountScale,
        hudCoinDigits_[1].baseColor,
        visible
    );
    UpdateStageStarHUD(deltaTime, visible);
    UpdatePrismBossHUD(deltaTime);
}

EnemyPrismSlime* GamePlayScene::FindPrismBossForHUD() {
    if (!objectManager_) {
        return nullptr;
    }

    for (const std::unique_ptr<Object3d>& object : objectManager_->GetObjects()) {
        auto* boss = dynamic_cast<EnemyPrismSlime*>(object.get());
        if (boss && boss->IsEncounterHudActive()) {
            return boss;
        }
    }
    return nullptr;
}

EnemyMagmaSlime* GamePlayScene::FindMagmaBossForHUD() {
    if (!objectManager_) {
        return nullptr;
    }

    for (const std::unique_ptr<Object3d>& object : objectManager_->GetObjects()) {
        auto* boss = dynamic_cast<EnemyMagmaSlime*>(object.get());
        if (boss && boss->IsEncounterHudActive()) {
            return boss;
        }
    }
    return nullptr;
}

EnemyFalseKingSlime* GamePlayScene::FindFalseKingBossForHUD() {
    if (!objectManager_) {
        return nullptr;
    }

    for (const std::unique_ptr<Object3d>& object : objectManager_->GetObjects()) {
        auto* boss = dynamic_cast<EnemyFalseKingSlime*>(object.get());
        if (boss && boss->IsEncounterHudActive()) {
            return boss;
        }
    }
    return nullptr;
}

void GamePlayScene::UpdatePrismBossHUD(float deltaTime) {
    constexpr float kIntroDuration = 1.62f;
    constexpr float kDismissDuration = 0.38f;
    deltaTime = (std::max)(0.0f, deltaTime);
    prismBossHudAnimationTimer_ = std::fmod(prismBossHudAnimationTimer_ + deltaTime, 1000.0f);

    EnemyPrismSlime* prismBoss = FindPrismBossForHUD();
    EnemyMagmaSlime* magmaBoss = prismBoss ? nullptr : FindMagmaBossForHUD();
    EnemyFalseKingSlime* falseKingBoss = (prismBoss || magmaBoss) ? nullptr : FindFalseKingBossForHUD();
    const BossHudTheme requestedTheme = falseKingBoss
        ? BossHudTheme::FalseKing
        : (magmaBoss ? BossHudTheme::Magma : BossHudTheme::Prism);
    const bool usesMagmaTheme = requestedTheme == BossHudTheme::Magma;
    const bool usesFalseKingTheme = requestedTheme == BossHudTheme::FalseKing;
    const bool requestedVisible = prismBoss != nullptr || magmaBoss != nullptr || falseKingBoss != nullptr;
    const float maximumHp = prismBoss
        ? prismBoss->GetEncounterMaximumHp()
        : (magmaBoss
            ? magmaBoss->GetEncounterMaximumHp()
            : (falseKingBoss ? falseKingBoss->GetEncounterMaximumHp() : 1.0f));
    const float currentHp = prismBoss
        ? (std::clamp)(prismBoss->GetEncounterCurrentHp(), 0.0f, maximumHp)
        : (magmaBoss
            ? (std::clamp)(magmaBoss->GetEncounterCurrentHp(), 0.0f, maximumHp)
            : (falseKingBoss ? (std::clamp)(falseKingBoss->GetEncounterCurrentHp(), 0.0f, maximumHp) : 0.0f));
    const float actualRate = (std::clamp)(currentHp / maximumHp, 0.0f, 1.0f);
    const int falseKingPhase = falseKingBoss
        ? (std::clamp)(falseKingBoss->GetBattlePhase(), 1, 3)
        : 1;
    const float falseKingPhaseTransition = falseKingBoss
        ? falseKingBoss->GetPhaseTransitionProgress()
        : 1.0f;

    if (requestedVisible && prismBossHudTheme_ != requestedTheme) {
        prismBossHudTheme_ = requestedTheme;
        const char* nameTexture = usesFalseKingTheme
            ? "ui/hud/boss/false_king_slime_name.png"
            : (usesMagmaTheme
                ? "ui/hud/boss/magma_slime_name.png"
                : "ui/hud/boss/prism_slime_name.png");
        const char* sideDecorationTexture = usesMagmaTheme
            ? "particle/flame_soft.png"
            : "particle/diamond_shard.png";
        SetFullTextureRect(prismBossHudName_.sprite, Sprite::LoadTexture(nameTexture));
        const uint32_t sideDecorationHandle = Sprite::LoadTexture(sideDecorationTexture);
        SetFullTextureRect(prismBossHudShardLeft_.sprite, sideDecorationHandle);
        SetFullTextureRect(prismBossHudShardRight_.sprite, sideDecorationHandle);
    }

    if (requestedVisible &&
        (prismBossHudPhase_ == PrismBossHudPhase::Hidden || prismBossHudPhase_ == PrismBossHudPhase::Dismissing)) {
        prismBossHudPhase_ = PrismBossHudPhase::Introducing;
        prismBossHudTimer_ = 0.0f;
        prismBossHudDisplayedRate_ = 0.0f;
        prismBossHudDelayedRate_ = 0.0f;
        prismBossHudPreviousHp_ = currentHp;
        prismBossHudDamagePulseTimer_ = 0.0f;
        prismBossHudDamageHoldTimer_ = 0.0f;
        falseKingBossHudPreviousPhase_ = falseKingPhase;
        falseKingBossHudPhasePulseTimer_ = 0.0f;
    } else if (!requestedVisible &&
        prismBossHudPhase_ != PrismBossHudPhase::Hidden &&
        prismBossHudPhase_ != PrismBossHudPhase::Dismissing) {
        prismBossHudPhase_ = PrismBossHudPhase::Dismissing;
        prismBossHudTimer_ = 0.0f;
    }

    float frameReveal = 0.0f;
    float nameReveal = 0.0f;
    float visualAlpha = 0.0f;
    switch (prismBossHudPhase_) {
    case PrismBossHudPhase::Hidden:
        break;
    case PrismBossHudPhase::Introducing: {
        prismBossHudTimer_ += deltaTime;
        const float appearanceProgress = prismBoss
            ? prismBoss->GetEncounterAppearanceProgress()
            : (magmaBoss
                ? magmaBoss->GetEncounterAppearanceProgress()
                : (falseKingBoss ? falseKingBoss->GetEncounterAppearanceProgress() : 1.0f));
        frameReveal = (std::max)(SmoothStep(prismBossHudTimer_ / 0.34f), SmoothStep(appearanceProgress * 1.25f));
        nameReveal = SmoothStep((prismBossHudTimer_ - 0.08f) / 0.32f);
        prismBossHudDisplayedRate_ = SmoothStep((prismBossHudTimer_ - 0.42f) / 1.06f);
        prismBossHudDelayedRate_ = prismBossHudDisplayedRate_;
        prismBossHudPreviousHp_ = currentHp;
        visualAlpha = 1.0f;
        if (prismBossHudTimer_ >= kIntroDuration) {
            prismBossHudPhase_ = PrismBossHudPhase::Active;
            prismBossHudTimer_ = 0.0f;
            prismBossHudDisplayedRate_ = actualRate;
            prismBossHudDelayedRate_ = actualRate;
        }
        break;
    }
    case PrismBossHudPhase::Active: {
        frameReveal = 1.0f;
        nameReveal = 1.0f;
        visualAlpha = 1.0f;

        const bool tookDamage = currentHp < prismBossHudPreviousHp_ - 0.01f;
        if (tookDamage) {
            const float previousRate = (std::clamp)(prismBossHudPreviousHp_ / maximumHp, 0.0f, 1.0f);
            prismBossHudDamagePulseTimer_ = 0.44f;
            prismBossHudDamageHoldTimer_ = 0.20f;
            prismBossHudDelayedRate_ = (std::max)(prismBossHudDelayedRate_, previousRate);
        }
        prismBossHudPreviousHp_ = currentHp;
        const float follow = 1.0f - std::exp(-deltaTime * 13.0f);
        prismBossHudDisplayedRate_ += (actualRate - prismBossHudDisplayedRate_) * follow;
        if (actualRate >= prismBossHudDelayedRate_) {
            prismBossHudDelayedRate_ = actualRate;
            prismBossHudDamageHoldTimer_ = 0.0f;
        } else if (prismBossHudDamageHoldTimer_ > 0.0f) {
            prismBossHudDamageHoldTimer_ = (std::max)(0.0f, prismBossHudDamageHoldTimer_ - deltaTime);
        } else {
            prismBossHudDelayedRate_ = (std::max)(actualRate, prismBossHudDelayedRate_ - deltaTime * 0.36f);
        }
        break;
    }
    case PrismBossHudPhase::Dismissing:
        prismBossHudTimer_ += deltaTime;
        frameReveal = 1.0f;
        nameReveal = 1.0f;
        visualAlpha = 1.0f - SmoothStep(prismBossHudTimer_ / kDismissDuration);
        if (prismBossHudTimer_ >= kDismissDuration) {
            prismBossHudPhase_ = PrismBossHudPhase::Hidden;
            visualAlpha = 0.0f;
        }
        break;
    }

    prismBossHudDamagePulseTimer_ = (std::max)(0.0f, prismBossHudDamagePulseTimer_ - deltaTime);
    falseKingBossHudPhasePulseTimer_ = (std::max)(0.0f, falseKingBossHudPhasePulseTimer_ - deltaTime);
    if (usesFalseKingTheme && falseKingPhase != falseKingBossHudPreviousPhase_) {
        falseKingBossHudPreviousPhase_ = falseKingPhase;
        falseKingBossHudPhasePulseTimer_ = 0.82f;
    }
    const bool hudVisible = visualAlpha > 0.001f && prismBossHudPhase_ != PrismBossHudPhase::Hidden;
    const float damagePulseRate = (std::clamp)(prismBossHudDamagePulseTimer_ / 0.44f, 0.0f, 1.0f);
    const float damageImpact = std::sin((1.0f - damagePulseRate) * kPi) * damagePulseRate;
    const float barShake = damagePulseRate > 0.0f
        ? std::sin(prismBossHudDamagePulseTimer_ * 96.0f) * SpriteLayoutScaler::ScaleDesignX(4.0f) * damagePulseRate
        : 0.0f;
    const float barScale = 0.12f + frameReveal * 0.88f;
    const float displayedRate = (std::clamp)(prismBossHudDisplayedRate_, 0.0f, 1.0f);
    const float delayedRate = (std::clamp)(prismBossHudDelayedRate_, displayedRate, 1.0f);
    const float lowHpRate = (std::clamp)((0.34f - displayedRate) / 0.34f, 0.0f, 1.0f);
    const float lowHpPulse = lowHpRate * (0.5f + 0.5f * std::sin(prismBossHudAnimationTimer_ * 7.0f));

    auto updateSprite = [](HudSpriteState& state, bool visible, const Vector2& position, const Vector2& size, const Vector4& color, float rotation = 0.0f) {
        if (!state.sprite) {
            return;
        }
        state.sprite->SetVisible(visible);
        state.sprite->SetPosition(position);
        state.sprite->SetSize(size);
        state.sprite->SetRotation(rotation);
        state.sprite->SetColor(color);
        state.sprite->Update();
    };

    const float phasePulseRate = (std::clamp)(falseKingBossHudPhasePulseTimer_ / 0.82f, 0.0f, 1.0f);
    const float phaseImpact = std::sin((1.0f - phasePulseRate) * kPi) * phasePulseRate;
    const float glowPulse = 0.82f + std::sin(prismBossHudAnimationTimer_ * 3.6f) * 0.18f + phaseImpact * 0.34f;
    const Vector3 glowColor = usesFalseKingTheme
        ? (falseKingPhase >= 3
            ? Vector3{ 0.72f, 0.22f + phaseImpact * 0.28f, 1.0f }
            : (falseKingPhase == 2
                ? Vector3{ 1.0f, 0.28f + phaseImpact * 0.22f, 0.72f }
                : Vector3{ 1.0f, 0.54f + lowHpPulse * 0.22f, 0.18f }))
        : (usesMagmaTheme
            ? Vector3{ 1.0f, 0.26f + lowHpPulse * 0.14f, 0.025f }
            : Vector3{ 0.34f + lowHpPulse * 0.30f, 0.58f, 1.0f });
    const Vector3 fillColor = usesFalseKingTheme
        ? (falseKingPhase >= 3
            ? Vector3{ 0.72f + phaseImpact * 0.22f, 0.16f, 1.0f }
            : (falseKingPhase == 2
                ? Vector3{ 1.0f, 0.34f, 0.72f + phaseImpact * 0.20f }
                : Vector3{ 1.0f, 0.72f - lowHpPulse * 0.32f, 0.12f + lowHpPulse * 0.18f }))
        : (usesMagmaTheme
            ? Vector3{ 1.0f, 0.48f - lowHpPulse * 0.30f, 0.035f }
            : Vector3{ 0.16f + lowHpPulse * 0.58f, 0.84f - lowHpPulse * 0.34f, 1.0f });
    const Vector3 frameTopColor = usesFalseKingTheme
        ? (falseKingPhase >= 3
            ? Vector3{ 0.88f, 0.52f, 1.0f }
            : (falseKingPhase == 2 ? Vector3{ 1.0f, 0.54f, 0.86f } : Vector3{ 1.0f, 0.90f, 0.42f }))
        : (usesMagmaTheme
            ? Vector3{ 1.0f, 0.88f, 0.32f }
            : Vector3{ 0.54f + lowHpPulse * 0.30f, 0.94f, 1.0f });
    const Vector3 frameBottomColor = usesFalseKingTheme
        ? (falseKingPhase >= 3
            ? Vector3{ 0.32f, 0.72f, 1.0f }
            : (falseKingPhase == 2 ? Vector3{ 0.72f, 0.20f, 1.0f } : Vector3{ 0.62f + lowHpPulse * 0.20f, 0.22f, 0.92f }))
        : (usesMagmaTheme
            ? Vector3{ 0.62f + lowHpPulse * 0.30f, 0.045f, 0.015f }
            : Vector3{ 0.66f + lowHpPulse * 0.24f, 0.44f, 1.0f });
    updateSprite(
        prismBossHudGlow_, hudVisible,
        { prismBossHudGlow_.basePosition.x + barShake, prismBossHudGlow_.basePosition.y },
        { prismBossHudGlow_.baseSize.x * barScale * (1.0f + damageImpact * 0.025f), prismBossHudGlow_.baseSize.y * (0.78f + frameReveal * 0.22f) },
        { glowColor.x, glowColor.y, glowColor.z, prismBossHudGlow_.baseColor.w * visualAlpha * glowPulse });
    updateSprite(
        prismBossHudBack_, hudVisible,
        { prismBossHudBack_.basePosition.x + barShake, prismBossHudBack_.basePosition.y },
        { prismBossHudBack_.baseSize.x * barScale, prismBossHudBack_.baseSize.y },
        { prismBossHudBack_.baseColor.x, prismBossHudBack_.baseColor.y, prismBossHudBack_.baseColor.z, prismBossHudBack_.baseColor.w * visualAlpha });
    updateSprite(
        prismBossHudTrack_, hudVisible,
        { prismBossHudTrack_.basePosition.x + barShake, prismBossHudTrack_.basePosition.y },
        { prismBossHudTrack_.baseSize.x * barScale, prismBossHudTrack_.baseSize.y },
        { prismBossHudTrack_.baseColor.x, prismBossHudTrack_.baseColor.y, prismBossHudTrack_.baseColor.z, prismBossHudTrack_.baseColor.w * visualAlpha });

    const float fullFillWidth = prismBossHudFill_.baseSize.x * barScale;
    const float fullFillLeft = prismBossHudFill_.basePosition.x + (prismBossHudFill_.baseSize.x - fullFillWidth) * 0.5f + barShake;
    const float damageFillWidth = prismBossHudDamageFill_.baseSize.x * barScale;
    const float damageFillLeft = prismBossHudDamageFill_.basePosition.x + (prismBossHudDamageFill_.baseSize.x - damageFillWidth) * 0.5f + barShake;
    const float highlightWidth = prismBossHudHighlight_.baseSize.x * barScale;
    const float highlightLeft = prismBossHudHighlight_.basePosition.x + (prismBossHudHighlight_.baseSize.x - highlightWidth) * 0.5f + barShake;
    updateSprite(
        prismBossHudDamageFill_, hudVisible && delayedRate > 0.001f,
        { damageFillLeft, prismBossHudDamageFill_.basePosition.y },
        { damageFillWidth * delayedRate, prismBossHudDamageFill_.baseSize.y },
        { 1.0f, usesFalseKingTheme ? 0.28f : (usesMagmaTheme ? 0.10f : 0.24f + lowHpPulse * 0.12f), usesFalseKingTheme ? 0.52f : (usesMagmaTheme ? 0.025f : 0.88f), prismBossHudDamageFill_.baseColor.w * visualAlpha });
    updateSprite(
        prismBossHudFill_, hudVisible && displayedRate > 0.001f,
        { fullFillLeft, prismBossHudFill_.basePosition.y },
        { fullFillWidth * displayedRate, prismBossHudFill_.baseSize.y * (1.0f + damageImpact * 0.09f) },
        { fillColor.x, fillColor.y, fillColor.z, prismBossHudFill_.baseColor.w * visualAlpha });
    updateSprite(
        prismBossHudHighlight_, hudVisible && displayedRate > 0.001f,
        { highlightLeft, prismBossHudHighlight_.basePosition.y },
        { highlightWidth * displayedRate, prismBossHudHighlight_.baseSize.y },
        { 1.0f, usesFalseKingTheme ? 0.96f : (usesMagmaTheme ? 0.86f : 1.0f), usesFalseKingTheme ? 0.72f : (usesMagmaTheme ? 0.36f : 1.0f), prismBossHudHighlight_.baseColor.w * visualAlpha * (0.74f + glowPulse * 0.26f) });
    updateSprite(
        prismBossHudFrameTop_, hudVisible,
        { prismBossHudFrameTop_.basePosition.x + barShake, prismBossHudFrameTop_.basePosition.y },
        { prismBossHudFrameTop_.baseSize.x * barScale, prismBossHudFrameTop_.baseSize.y },
        { frameTopColor.x, frameTopColor.y, frameTopColor.z, prismBossHudFrameTop_.baseColor.w * visualAlpha });
    updateSprite(
        prismBossHudFrameBottom_, hudVisible,
        { prismBossHudFrameBottom_.basePosition.x + barShake, prismBossHudFrameBottom_.basePosition.y },
        { prismBossHudFrameBottom_.baseSize.x * barScale, prismBossHudFrameBottom_.baseSize.y },
        { frameBottomColor.x, frameBottomColor.y, frameBottomColor.z, prismBossHudFrameBottom_.baseColor.w * visualAlpha });

    const float shardScale = (0.55f + frameReveal * 0.45f) * (1.0f + damageImpact * 0.13f);
    const float shardAlpha = visualAlpha * frameReveal;
    updateSprite(
        prismBossHudShardLeft_, hudVisible,
        { prismBossHudShardLeft_.basePosition.x + barShake, prismBossHudShardLeft_.basePosition.y },
        { prismBossHudShardLeft_.baseSize.x * shardScale, prismBossHudShardLeft_.baseSize.y * shardScale },
        { usesFalseKingTheme ? 1.0f : (usesMagmaTheme ? 1.0f : 0.42f + lowHpPulse * 0.35f), usesFalseKingTheme ? 0.68f : (usesMagmaTheme ? 0.31f : 0.90f), usesFalseKingTheme ? 0.18f : (usesMagmaTheme ? 0.025f : 1.0f), prismBossHudShardLeft_.baseColor.w * shardAlpha },
        -0.18f + std::sin(prismBossHudAnimationTimer_ * 2.8f) * 0.025f);
    updateSprite(
        prismBossHudShardRight_, hudVisible,
        { prismBossHudShardRight_.basePosition.x + barShake, prismBossHudShardRight_.basePosition.y },
        { prismBossHudShardRight_.baseSize.x * shardScale, prismBossHudShardRight_.baseSize.y * shardScale },
        { usesFalseKingTheme ? 0.72f : (usesMagmaTheme ? 1.0f : 0.76f + lowHpPulse * 0.20f), usesFalseKingTheme ? 0.24f : (usesMagmaTheme ? 0.66f : 0.42f), usesFalseKingTheme ? 1.0f : (usesMagmaTheme ? 0.08f : 1.0f), prismBossHudShardRight_.baseColor.w * shardAlpha },
        0.18f - std::sin(prismBossHudAnimationTimer_ * 2.8f) * 0.025f);

    const float nameScale = 0.82f + nameReveal * 0.18f + damageImpact * 0.035f;
    updateSprite(
        prismBossHudName_, hudVisible && nameReveal > 0.001f,
        { prismBossHudName_.basePosition.x + barShake * 0.45f, prismBossHudName_.basePosition.y - SpriteLayoutScaler::ScaleDesignY((1.0f - nameReveal) * 11.0f) },
        { prismBossHudName_.baseSize.x * nameScale, prismBossHudName_.baseSize.y * nameScale },
        { 1.0f, usesFalseKingTheme ? 0.96f : (usesMagmaTheme ? 0.96f : 1.0f), usesFalseKingTheme ? 0.84f : (usesMagmaTheme ? 0.78f : 1.0f), prismBossHudName_.baseColor.w * visualAlpha * nameReveal });

    const bool showFalseKingPhases = hudVisible && usesFalseKingTheme && frameReveal > 0.10f;
    for (HudSpriteState& divider : falseKingBossHudPhaseDividers_) {
        const float dividerGlow = 0.72f + std::sin(prismBossHudAnimationTimer_ * 4.2f) * 0.18f;
        updateSprite(
            divider,
            showFalseKingPhases,
            { divider.basePosition.x + barShake, divider.basePosition.y },
            { divider.baseSize.x * (1.0f + phaseImpact * 0.42f), divider.baseSize.y * frameReveal },
            { 1.0f, falseKingPhase >= 3 ? 0.54f : 0.84f, falseKingPhase >= 3 ? 1.0f : 0.28f,
              visualAlpha * frameReveal * dividerGlow });
    }
    for (size_t i = 0; i < falseKingBossHudPhaseCrowns_.size(); ++i) {
        HudSpriteState& crown = falseKingBossHudPhaseCrowns_[i];
        const int markerPhase = static_cast<int>(i) + 1;
        const bool currentPhase = markerPhase == falseKingPhase;
        const bool clearedPhase = markerPhase < falseKingPhase;
        const float markerPulse = currentPhase
            ? 0.5f + 0.5f * std::sin(prismBossHudAnimationTimer_ * 5.6f)
            : 0.0f;
        const float transitionPulse = currentPhase
            ? phaseImpact + (1.0f - falseKingPhaseTransition) * 0.12f
            : 0.0f;
        const float markerScale = (currentPhase ? 1.0f + markerPulse * 0.10f + transitionPulse * 0.26f : 0.76f) *
            (0.72f + nameReveal * 0.28f);
        Vector4 markerColor;
        if (currentPhase) {
            markerColor = falseKingPhase >= 3
                ? Vector4{ 0.88f, 0.48f, 1.0f, visualAlpha * (0.86f + markerPulse * 0.14f) }
                : Vector4{ 1.0f, 0.82f, 0.24f, visualAlpha * (0.86f + markerPulse * 0.14f) };
        } else if (clearedPhase) {
            markerColor = { 0.72f, 0.44f, 0.92f, visualAlpha * 0.54f };
        } else {
            markerColor = { 0.22f, 0.10f, 0.34f, visualAlpha * 0.46f };
        }
        updateSprite(
            crown,
            showFalseKingPhases,
            { crown.basePosition.x + barShake, crown.basePosition.y - markerPulse * SpriteLayoutScaler::ScaleDesignY(2.5f) },
            { crown.baseSize.x * markerScale, crown.baseSize.y * markerScale },
            markerColor,
            currentPhase ? std::sin(prismBossHudAnimationTimer_ * 4.2f) * 0.035f : 0.0f);
    }

    for (size_t i = 0; i < prismBossHudGlints_.size(); ++i) {
        HudSpriteState& glint = prismBossHudGlints_[i];
        const float phase = prismBossHudAnimationTimer_ * (3.2f + static_cast<float>(i) * 0.17f) + static_cast<float>(i) * 1.31f;
        const float sparkle = std::pow((std::max)(0.0f, std::sin(phase)), 7.0f);
        Vector2 position = glint.basePosition;
        if (i == prismBossHudGlints_.size() - 1) {
            position.x = fullFillLeft + fullFillWidth * displayedRate;
            position.y = prismBossHudFill_.basePosition.y;
        }
        position.x += barShake;
        const float glintScale = 0.55f + sparkle * 1.15f + (i == prismBossHudGlints_.size() - 1 ? 0.30f : 0.0f);
        const float glintAlpha = visualAlpha * frameReveal * (0.08f + sparkle * 0.92f);
        updateSprite(
            glint, hudVisible && frameReveal > 0.1f,
            position,
            { glint.baseSize.x * glintScale, glint.baseSize.y * glintScale },
            { usesFalseKingTheme ? 1.0f : (usesMagmaTheme ? 1.0f : 0.72f + lowHpPulse * 0.20f), usesFalseKingTheme ? 0.78f : (usesMagmaTheme ? 0.58f : 0.96f), usesFalseKingTheme ? 0.28f : (usesMagmaTheme ? 0.12f : 1.0f), glint.baseColor.w * glintAlpha },
            phase * 0.18f);
    }
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
        lifeLostSlimeObject_->SetIsVisible(false);
    }
    if (lifeLostStunObject_) {
        lifeLostStunObject_->SetIsVisible(false);
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
    DrawPrismBossHUD();
    DrawGameplayHUDSprite(hudHpFrame_);
    DrawGameplayHUDSprite(hudHpDamageFill_);
    DrawGameplayHUDSprite(hudHpFill_);
    DrawGameplayHUDSprite(hudHpHighlight_);
    DrawGameplayHUDSprite(hudHpIcon_);
    DrawGameplayHUDSprite(hudMorphGaugeBack_);
    DrawGameplayHUDSprite(hudMorphGaugeFill_);
    DrawGameplayHUDSprite(hudMorphGaugeIcon_);
    DrawGameplayHUDSprite(hudMorphGaugeFrame_);
    DrawLifeLostPresentation();
}

void GamePlayScene::DrawPrismBossHUD() {
    DrawGameplayHUDSprite(prismBossHudGlow_);
    DrawGameplayHUDSprite(prismBossHudBack_);
    DrawGameplayHUDSprite(prismBossHudTrack_);
    DrawGameplayHUDSprite(prismBossHudDamageFill_);
    DrawGameplayHUDSprite(prismBossHudFill_);
    DrawGameplayHUDSprite(prismBossHudHighlight_);
    DrawGameplayHUDSprite(prismBossHudFrameTop_);
    DrawGameplayHUDSprite(prismBossHudFrameBottom_);
    DrawGameplayHUDSprite(prismBossHudShardLeft_);
    DrawGameplayHUDSprite(prismBossHudShardRight_);
    DrawGameplayHUDSprite(prismBossHudName_);
    for (const HudSpriteState& divider : falseKingBossHudPhaseDividers_) {
        DrawGameplayHUDSprite(divider);
    }
    for (const HudSpriteState& crown : falseKingBossHudPhaseCrowns_) {
        DrawGameplayHUDSprite(crown);
    }
    for (const HudSpriteState& glint : prismBossHudGlints_) {
        DrawGameplayHUDSprite(glint);
    }
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

    if (lifeLostSlimeObject_) {
        lifeLostSlimeObject_->SetIsVisible(false);
    }

    if (lifeLostStunObject_) {
        lifeLostStunObject_->SetIsVisible(false);
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

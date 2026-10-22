#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/ui/SaveIndicatorOverlay.h"

#include "CameraManager.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/actor/enemy/boss/magma/EnemyMagmaSlime.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "game/crown/CrownFinaleTuning.h"
#include "game/system/progress/GameDataManager.h"
#include "game/system/scene/LevelLoader.h"
#include "Log.h"
#include "Sprite.h"
#include "game/ui/SpriteLayoutScaler.h"
#include "game/system/progress/StageManager.h"
#include "TextureManager.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kDamagePulseDuration = 0.42f;
constexpr float kHurtIconDuration = 0.48f;
constexpr float kHpDamageHoldDuration = 0.18f;
constexpr float kLifeGainPulseDuration = 0.62f;
constexpr float kCoinPulseDuration = 0.24f;

// HUDの表示率を0～1へ収め、両端を滑らかにつなぎます。
float SmoothStep(float value) {
    // 星がHUDへ飛ぶ移動の両端を滑らかにし、急な開始・停止を防ぐ。
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

engine::Vector2 Lerp(const engine::Vector2& a, const engine::Vector2& b, float t) {
    // HUDの二点間移動に共通の補間率を使う。
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

engine::Vector2 QuadraticBezier(const engine::Vector2& start, const engine::Vector2& control, const engine::Vector2& end, float t) {
    // 取得した星を画面上で弧を描かせ、HUDの枠へ導く。
    return Lerp(Lerp(start, control, t), Lerp(control, end, t), t);
}

void SetFullTextureRect(engine::Sprite* sprite, uint32_t textureHandle) {
    // アイコン差し替え時に画像全体を使い、旧画像のUV領域を残さない。
    if (!sprite || textureHandle == 0) {
        return;
    }

    sprite->SetTextureHandle(textureHandle);
    const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle);
    sprite->SetTextureRect({0.0f, 0.0f}, {static_cast<float>(metadata.width), static_cast<float>(metadata.height)});
}

const char* ResolveHpIconTexture(const Player* player) {
    // 現在の本体と同じ装飾付き肖像を表示し、被弾時も能力の目印を維持する。
    if (player && player->IsEnemyMorphed()) {
        switch (player->GetEnemyMorphType()) {
        case Player::EnemyMorphType::Slime: return "ui/hud/copy_portraits/pink.png";
        case Player::EnemyMorphType::FireSlime: return "ui/hud/copy_portraits/fire.png";
        case Player::EnemyMorphType::ThunderSlime: return "ui/hud/copy_portraits/thunder.png";
        case Player::EnemyMorphType::WindSlime: return "ui/hud/copy_portraits/wind.png";
        case Player::EnemyMorphType::Bomber: return "ui/hud/copy_portraits/bomb.png";
        case Player::EnemyMorphType::Crown: return "ui/hud/copy_portraits/crown.png";
        default: break;
        }
    }
    return "ui/hud/copy_portraits/normal.png";
}

} // namespace

// HUD はシーンの現在状態を読み取って更新します。収集演出とボス表示はゲーム進行の判定を書き換えません。

void GamePlayScene::UpdateUI(float deltaTime) {
    // ゲーム進行に合わせて通常HUDとボスHUDの表示を更新する。
    UpdateGameplayHUD(deltaTime);
    UpdateLifeLostPresentation(deltaTime);

    if (saveIndicatorOverlay_) {
        saveIndicatorOverlay_->Update(deltaTime);
    }
}

GamePlayScene::HudSpriteState GamePlayScene::BindHud(const std::string& name) {
    engine::Sprite* sprite = GetSpriteByName(name);
    if (!sprite) {
        // 配置漏れを静かに隠さず、Editorで修正できるよう名前をログに残す。
        LOG_ERROR("gameplayHUD.json: Sprite '{}' was not found", name);
        return {};
    }

    sprite->SetVisible(true);
    sprite->Update();

    return {sprite, sprite->GetPosition(), sprite->GetSize(), sprite->GetColor()};
}

void GamePlayScene::DrawGameplayHUDSprite(const HudSpriteState& state) {
    // レイアウトで束ねたSpriteが可視の場合だけ描画する。
    if (state.sprite) {
        state.sprite->Draw();
    }
}

bool GamePlayScene::IsGameplayHUDSprite(const engine::Sprite* sprite) const {
    // 通常シーンのSpriteとHUD管理下のSpriteを区別し、二重描画を防ぐ。
    if (!sprite) {
        return false;
    }

    const std::string& name = sprite->GetName();
    return name.rfind("hud_", 0) == 0 || name.rfind("life_lost_", 0) == 0;
}

void GamePlayScene::InitializeGameplayHUD() {
    // 静的な画像・配置・色はレイアウトから読み、ここでは演出用の参照だけ結び付ける。
    if (levelLoader_) {
        levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/gameplayHUD.json");
    }

    // プレイヤーHPは現在値、遅れて追うダメージ量、枠と光を別Spriteで重ねる。
    hudHpIcon_ = BindHud("hud_hp_icon");
    // 肖像は正方形のまま枠内へ収め、ウィンドウ比率による顔の潰れを防ぐ。
    const float portraitSide = (std::min)(hudHpIcon_.baseSize.x, hudHpIcon_.baseSize.y);
    hudHpIcon_.baseSize = {portraitSide, portraitSide};

    hudCopyStyle_ = BindHud("hud_copy_style");
    copyStyleHud_.Bind(hudCopyStyle_.sprite);
    hudHpDamageFill_ = BindHud("hud_hp_damage_fill");
    hudHpFill_ = BindHud("hud_hp_fill");
    if (hudHpFill_.sprite) {
        hudHpFill_.sprite->SetAnimation(8, 0.08f, true);
        hudHpFill_.sprite->Play();
        hudHpFill_.sprite->SetSize(hudHpFill_.baseSize);
        hudHpFill_.sprite->Update();
    }

    hudHpHighlight_ = BindHud("hud_hp_highlight");
    if (hudHpHighlight_.sprite) {
        hudHpHighlight_.sprite->SetAnimation(6, 0.10f, true);
        hudHpHighlight_.sprite->Play();
        hudHpHighlight_.sprite->SetSize(hudHpHighlight_.baseSize);
        hudHpHighlight_.sprite->Update();
    }

    hudHpFrame_ = BindHud("hud_hp_frame");

    // ボスHUDは共通の表示部品を使い、戦闘相手に応じて後から色と名前を替える。
    prismBossHudGlow_ = BindHud("hud_boss_prism_glow");
    prismBossHudBack_ = BindHud("hud_boss_prism_back");
    prismBossHudTrack_ = BindHud("hud_boss_prism_track");
    prismBossHudDamageFill_ = BindHud("hud_boss_prism_damage_fill");
    prismBossHudFill_ = BindHud("hud_boss_prism_fill");
    prismBossHudHighlight_ = BindHud("hud_boss_prism_highlight");
    prismBossHudFrameTop_ = BindHud("hud_boss_prism_frame_top");
    prismBossHudFrameBottom_ = BindHud("hud_boss_prism_frame_bottom");
    prismBossHudShardLeft_ = BindHud("hud_boss_prism_shard_left");
    prismBossHudShardRight_ = BindHud("hud_boss_prism_shard_right");
    prismBossHudName_ = BindHud("hud_boss_prism_name");

    // 装飾はJSONの基準位置を保持し、更新時に揺れを加算する。
    for (size_t i = 0; i < prismBossHudGlints_.size(); ++i) {
        prismBossHudGlints_[i] = BindHud("hud_boss_prism_glint_" + std::to_string(i));
    }

    // 偽王専用の段階表示と詠唱ゲージも先に生成し、他のボスでは非表示にする。
    for (size_t i = 0; i < falseKingBossHudPhaseCrowns_.size(); ++i) {
        falseKingBossHudPhaseCrowns_[i] = BindHud("hud_boss_false_king_phase_crown_" + std::to_string(i));
    }

    for (size_t i = 0; i < falseKingBossHudPhaseDividers_.size(); ++i) {
        falseKingBossHudPhaseDividers_[i] = BindHud("hud_boss_false_king_phase_divider_" + std::to_string(i));
    }

    falseKingCastTrack_ = BindHud("hud_false_king_cast_track");
    falseKingCastFill_ = BindHud("hud_false_king_cast_fill");

    // 残機・コインは数字を桁ごとに持ち、収集スターは保存済み状態と今回の取得を合わせて初期化する。
    hudLifeIcon_ = BindHud("hud_life_icon");
    hudLifeXIcon_ = BindHud("hud_life_x");

    for (size_t i = 0; i < hudLifeDigits_.size(); ++i) {
        hudLifeDigits_[i] = BindHud("hud_life_digit_" + std::to_string(i));
    }

    hudCoinIcon_ = BindHud("hud_coin_icon");
    hudCoinXIcon_ = BindHud("hud_coin_x");
    for (size_t i = 0; i < hudCoinDigits_.size(); ++i) {
        hudCoinDigits_[i] = BindHud("hud_coin_digit_" + std::to_string(i));
    }

    const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();

    for (size_t i = 0; i < hudStageStarSlots_.size(); ++i) {
        hudStageStarSlots_[i] = BindHud("hud_stage_star_slot_" + std::to_string(i));
        hudStageStarVisualCollected_[i] = sessionStarCoins_[i] || GameDataManager::GetInstance()->IsStarCoinCollected(
                                                                      stageIndex, static_cast<int>(i));
        hudStageStarPulseTimers_[i] = 0.0f;
    }

    hudStageStarFlyParticles_.clear();
    hudPreviousLives_ = GameDataManager::GetInstance()->GetLives();
    hudPreviousCoins_ = GameDataManager::GetInstance()->GetCoins();
    hudLifeGainPulseTimer_ = 0.0f;
    hudCoinPulseTimer_ = 0.0f;

    // 残機減少の演出は通常HUDと独立した全画面レイヤーとして用意する。
    lifeLostIcon_ = BindHud("life_lost_icon");
    lifeLostXIcon_ = BindHud("life_lost_x");
    for (size_t i = 0; i < lifeLostDigits_.size(); ++i) {
        lifeLostDigits_[i] = BindHud("life_lost_digit_" + std::to_string(i));
    }

    lifeLostBackdrop_ = BindHud("life_lost_backdrop");
    InitializeLifeLostPresentationObjects();

    // 最初のフレームでダメージ演出が発生しないよう、比較に使う前回値を現在値へ揃える。
    lifeLostState_.Reset();

    hudPreviousHp_ = player_ ? player_->GetHp() : 0.0f;
    {
        const float maxHp = player_ ? std::max(player_->GetMaxHp(), 1.0f) : 1.0f;
        hudHpDelayedRate_ = player_ ? std::clamp(hudPreviousHp_ / maxHp, 0.0f, 1.0f) : 0.0f;
    }

    hudDamagePulseTimer_ = 0.0f;
    hudHurtIconTimer_ = 0.0f;
    hudHpDamageHoldTimer_ = 0.0f;
    hudHpAnimationTimer_ = 0.0f;
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

void GamePlayScene::SetGameplayHUDNumber(std::array<HudSpriteState, 2>& digits, int value,
                                         const engine::Vector2& rightAlignedPosition, float digitHeight, const engine::Vector4& color,
                                         bool visible) {
    // 一桁・二桁の数字を右揃えで描き、値の桁数で他のHUD要素を動かさない。
    value = std::clamp(value, 0, 99);

    const std::array<int, 2> digitValues = {value / 10, value % 10};
    const int digitCount = value >= 10 ? 2 : 1;
    const float digitWidth = digitHeight * 0.68f;
    const float spacing = digitWidth * 0.82f;
    const float totalWidth = digitCount == 2 ? spacing + digitWidth : digitWidth;
    const float startX = rightAlignedPosition.x - totalWidth + digitWidth * 0.5f;

    for (int i = 0; i < 2; ++i) {
        engine::Sprite* sprite = digits[i].sprite;
        if (!sprite)
            continue;

        const bool digitVisible = visible && (digitCount == 2 || i == 1);
        sprite->SetVisible(digitVisible);

        if (!digitVisible) {
            continue;
        }

        const int sourceIndex = digitCount == 2 ? i : 1;
        const int digit = digitValues[sourceIndex];
        const uint32_t handle = engine::Sprite::LoadTexture("number/" + std::to_string(digit) + ".png");
        sprite->SetTextureHandle(handle);
        sprite->SetPosition({startX + (sourceIndex - (2 - digitCount)) * spacing, rightAlignedPosition.y});
        sprite->SetSize({digitWidth, digitHeight});
        sprite->SetColor(color);
        sprite->Update();
    }
}

void GamePlayScene::CollectStarCoin(int coinIndex) {
    // 演出位置が無い取得ではセッション状態を即時反映し、該当枠を短く光らせる。
    if (coinIndex < 0 || coinIndex >= 3) {
        return;
    }

    sessionStarCoins_[coinIndex] = true;
    hudStageStarVisualCollected_[coinIndex] = true;
    hudStageStarPulseTimers_[coinIndex] = 0.45f;
}

void GamePlayScene::CollectStarCoin(int coinIndex, const engine::Vector3& worldPosition) {
    // 初回取得だけワールド位置からHUDへ飛ばし、再取得は即時表示にする。
    if (coinIndex < 0 || coinIndex >= 3) {
        return;
    }

    const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
    const bool alreadyCollected =
        sessionStarCoins_[coinIndex] || GameDataManager::GetInstance()->IsStarCoinCollected(stageIndex, coinIndex);

    sessionStarCoins_[coinIndex] = true;

    if (alreadyCollected) {
        hudStageStarVisualCollected_[coinIndex] = true;
        hudStageStarPulseTimers_[coinIndex] = 0.45f;
        return;
    }

    hudStageStarVisualCollected_[coinIndex] = false;
    StartStageStarHUDCollectEffect(coinIndex, worldPosition);
}

engine::Vector2 GamePlayScene::ProjectWorldToScreen(const engine::Vector3& worldPosition) const {
    // ワールド上の取得位置を画面座標へ変換し、カメラ不在や異常値では中央を使う。
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    const float screenW = static_cast<float>(WinApp::kClientWidth);
    const float screenH = static_cast<float>(WinApp::kClientHeight);

    if (!camera) {
        return {screenW * 0.5f, screenH * 0.5f};
    }

    engine::Vector3 ndc = engine::Math::Transform(worldPosition, camera->GetViewProjectionMatrix());

    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y)) {
        return {screenW * 0.5f, screenH * 0.5f};
    }

    return {std::clamp((ndc.x + 1.0f) * 0.5f * screenW, 24.0f, screenW - 24.0f),
            std::clamp((1.0f - ndc.y) * 0.5f * screenH, 24.0f, screenH - 24.0f)};
}

void GamePlayScene::StartStageStarHUDCollectEffect(int starIndex, const engine::Vector3& worldPosition) {
    // 星本体と火花を取得位置からHUD枠へ飛ばし、到着時に取得済み表示へ切り替える。
    if (starIndex < 0 || starIndex >= static_cast<int>(hudStageStarSlots_.size()) || !spriteCommon_) {
        if (starIndex >= 0 && starIndex < 3) {
            hudStageStarVisualCollected_[starIndex] = true;
            hudStageStarPulseTimers_[starIndex] = 0.45f;
        }

        return;
    }

    const engine::Vector2 start = ProjectWorldToScreen(worldPosition);
    const engine::Vector2 end = hudStageStarSlots_[starIndex].sprite
                            ? hudStageStarSlots_[starIndex].basePosition
                            : engine::Vector2{52.0f + 48.0f * static_cast<float>(starIndex), 214.0f};

    auto addParticle = [&](const std::string& texturePath, const engine::Vector2& particleStart, const engine::Vector2& particleEnd,
                           const engine::Vector2& control, float duration, float baseSize, float rotationSpeed,
                           bool fillsSlot) {
        StageStarUIFlyParticle particle;
        particle.sprite = std::make_unique<engine::Sprite>();
        particle.sprite->Initialize(spriteCommon_.get(), texturePath);
        particle.sprite->SetAnchorPoint({0.5f, 0.5f});
        particle.sprite->SetPosition(particleStart);
        particle.sprite->SetSize({baseSize, baseSize});
        particle.sprite->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
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

    const engine::Vector2 mainControl = {(start.x + end.x) * 0.5f,
                                 std::min(start.y, end.y) - 135.0f - 18.0f * static_cast<float>(starIndex)};
    addParticle("Resources/sprite/ui/hud/stage_star_filled.png", start, end, mainControl, 0.72f, 44.0f, 5.6f, true);

    for (int i = 0; i < 10; ++i) {
        const float angle = static_cast<float>(i) / 10.0f * kPi * 2.0f;
        const float radius = 18.0f + static_cast<float>((i % 3) * 7);
        const engine::Vector2 startOffset = {static_cast<float>(std::cos(angle) * radius),
                                     static_cast<float>(std::sin(angle) * radius)};
        const engine::Vector2 endOffset = {static_cast<float>(std::cos(angle + 0.9f) * 14.0f),
                                   static_cast<float>(std::sin(angle + 0.9f) * 14.0f)};
        const float lane = static_cast<float>(i - 5);
        const engine::Vector2 control = {(start.x + end.x) * 0.5f + lane * 14.0f,
                                 std::min(start.y, end.y) - 82.0f - std::abs(lane) * 4.0f};
        addParticle("Resources/sprite/ui/hud/stage_star_spark.png", {start.x + startOffset.x, start.y + startOffset.y},
                    {end.x + endOffset.x, end.y + endOffset.y}, control, 0.48f + 0.035f * static_cast<float>(i % 4),
                    14.0f + static_cast<float>(i % 3) * 3.0f, 6.5f + static_cast<float>(i % 5), false);
    }
}

void GamePlayScene::UpdateStageStarHUD(float deltaTime, bool visible) {
    // 飛行中の星を進め、到着後は保存済みの取得状態と枠の表示を同期する。
    visible = visible && StageManager::GetInstance()->GetCurrentStage().hasStarCoins;
    const int stageIndex = StageManager::GetInstance()->GetCurrentStageIndex();

    for (auto it = hudStageStarFlyParticles_.begin(); it != hudStageStarFlyParticles_.end();) {
        StageStarUIFlyParticle& particle = *it;
        particle.timer += deltaTime;
        const float t = particle.duration > 0.0f ? std::clamp(particle.timer / particle.duration, 0.0f, 1.0f) : 1.0f;
        const float eased = SmoothStep(t);
        const engine::Vector2 position = QuadraticBezier(particle.start, particle.control, particle.end, eased);
        const float fade = particle.fillsSlot ? 1.0f : std::clamp(1.0f - (t - 0.62f) / 0.38f, 0.0f, 1.0f);
        const float size = particle.baseSize * (particle.fillsSlot ? (1.0f + (1.0f - t) * 0.20f) : (1.0f - t * 0.38f));

        if (particle.sprite) {
            particle.sprite->SetVisible(visible && fade > 0.0f);
            particle.sprite->SetPosition(position);
            particle.sprite->SetSize({size, size});
            particle.sprite->SetRotation(particle.rotationSpeed * particle.timer);
            particle.sprite->SetColor({1.0f, 1.0f, 1.0f, visible ? fade : 0.0f});
            particle.sprite->Update();
        }

        if (t >= 1.0f) {
            if (particle.fillsSlot && particle.starIndex >= 0 && particle.starIndex < 3) {
                hudStageStarVisualCollected_[particle.starIndex] = true;
                hudStageStarPulseTimers_[particle.starIndex] = 0.65f;
            }

            it = hudStageStarFlyParticles_.erase(it);
        } else {
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
        const uint32_t handle =
            engine::Sprite::LoadTexture(collected ? "ui/hud/stage_star_filled.png" : "ui/hud/stage_star_empty.png");
        const float pulseT = hudStageStarPulseTimers_[i] > 0.0f ? hudStageStarPulseTimers_[i] / 0.65f : 0.0f;
        const float scale = 1.0f + std::sin((1.0f - pulseT) * kPi) * pulseT * 0.28f;
        const float alpha = visible ? (collected ? slot.baseColor.w : slot.baseColor.w * 0.78f) : 0.0f;

        slot.sprite->SetTextureHandle(handle);
        slot.sprite->SetVisible(visible);
        slot.sprite->SetPosition(slot.basePosition);
        slot.sprite->SetSize({slot.baseSize.x * scale, slot.baseSize.y * scale});
        slot.sprite->SetRotation(collected ? std::sin(hudStageStarPulseTimers_[i] * 12.0f) * pulseT * 0.10f : 0.0f);
        slot.sprite->SetColor({slot.baseColor.x, slot.baseColor.y, slot.baseColor.z, alpha});
        slot.sprite->Update();
    }
}

// 数値の即時更新と、被弾・獲得時に少し遅れて追う表示を分けて更新します。
void GamePlayScene::UpdateGameplayHUD(float deltaTime) {
    deltaTime = (std::max)(0.0f, deltaTime);
    copyStyleHud_.Update(player_, deltaTime);
    const bool visible = player_ != nullptr;
    const float maxHp = player_ ? std::max(player_->GetMaxHp(), 1.0f) : 1.0f;
    const float hp = player_ ? std::clamp(player_->GetHp(), 0.0f, maxHp) : 0.0f;
    const float hpRate = std::clamp(hp / maxHp, 0.0f, 1.0f);
    const float previousHpRate = std::clamp(hudPreviousHp_ / maxHp, 0.0f, 1.0f);
    const int lives = GameDataManager::GetInstance()->GetLives();
    const int coins = GameDataManager::GetInstance()->GetCoins();

    // 数値変化を短い反応として保持し、残機・コイン・HP に別の見せ方を与えます。
    if (visible && lives > hudPreviousLives_) {
        hudLifeGainPulseTimer_ = kLifeGainPulseDuration;
    }

    if (visible && coins != hudPreviousCoins_) {
        hudCoinPulseTimer_ = kCoinPulseDuration;
    }

    hudPreviousLives_ = lives;
    hudPreviousCoins_ = coins;

    const bool tookDamage = visible && hp < hudPreviousHp_ - 0.01f;

    if (tookDamage) {
        hudDamagePulseTimer_ = kDamagePulseDuration;
        hudHurtIconTimer_ = kHurtIconDuration;
        hudHpDamageHoldTimer_ = kHpDamageHoldDuration;
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
    const float damageRate = std::clamp(hudDamagePulseTimer_ / kDamagePulseDuration, 0.0f, 1.0f);
    const float damageImpact = std::sin((1.0f - damageRate) * kPi) * damageRate;
    const float lowHpRate = visible ? std::clamp((0.34f - hpRate) / 0.34f, 0.0f, 1.0f) : 0.0f;
    const float lowHpPulse = lowHpRate * (0.5f + 0.5f * std::sin(hudHpAnimationTimer_ * (5.0f + lowHpRate * 3.0f)));
    const float liquidWave = std::sin(hudHpAnimationTimer_ * 4.6f);
    const float idleIconPulse = 1.0f + std::sin(hudHpAnimationTimer_ * 2.2f) * 0.012f + lowHpPulse * 0.035f;
    const float hpIconPulse = idleIconPulse + std::abs(pulse) * damageRate * 0.07f;
    const float barShake =
        damageRate > 0.0f ? std::sin(hudDamagePulseTimer_ * 92.0f) * SpriteLayoutScaler::ScaleDesignX(3.2f) * damageRate
                          : 0.0f;
    const engine::Vector2 liquidOffset = {barShake, 0.0f};
    const float liquidHeightScale = 1.0f + damageImpact * 0.07f;
    const float lifeGainRate =
        hudLifeGainPulseTimer_ > 0.0f ? hudLifeGainPulseTimer_ / kLifeGainPulseDuration : 0.0f;
    const float lifeGainWave = std::sin((1.0f - lifeGainRate) * kPi * 2.0f) * lifeGainRate;
    const float lifeCountScaleX = 1.0f + lifeGainWave * 0.28f + lifeGainRate * 0.06f;
    const float lifeCountScaleY = 1.0f - lifeGainWave * 0.18f + lifeGainRate * 0.03f;
    const float coinPulseRate = hudCoinPulseTimer_ > 0.0f ? hudCoinPulseTimer_ / kCoinPulseDuration : 0.0f;
    const float coinPulsePhase = 1.0f - coinPulseRate;
    const float coinBounce = std::sin(coinPulsePhase * kPi) * coinPulseRate;
    const float coinPulseScaleX = 1.0f + coinBounce * 0.20f;
    const float coinPulseScaleY = 1.0f + coinBounce * 0.28f;
    const float coinCountScale = 1.0f + coinBounce * 0.14f;
    const float coinBounceOffsetY = -12.0f * coinBounce;

    const bool damageReacting = hudHurtIconTimer_ > 0.0f;
    const float hpIconShakeX =
        damageReacting ? std::sin(hudHurtIconTimer_ * 85.0f) * SpriteLayoutScaler::ScaleDesignX(3.0f) : 0.0f;
    const float hpIconShakeY =
        damageReacting ? std::sin(hudHurtIconTimer_ * 61.0f) * SpriteLayoutScaler::ScaleDesignY(1.2f) : 0.0f;
    const float hpIconRotation =
        damageReacting ? std::sin(hudHurtIconTimer_ * 55.0f) * 0.055f : liquidWave * lowHpRate * 0.012f;

    // HP 表示は実値と遅れて追うダメージ量を別レイヤーにし、被弾量を読めるようにします。
    if (hudHpIcon_.sprite) {
        const uint32_t handle = engine::Sprite::LoadTexture(ResolveHpIconTexture(player_));
        SetFullTextureRect(hudHpIcon_.sprite, handle);
        hudHpIcon_.sprite->SetVisible(visible);
        // 枠と同じ揺れ・拡縮中心へ追従し、装飾を丸枠の内側に保つ。
        hudHpIcon_.sprite->SetPosition(
            {hudHpIcon_.basePosition.x + barShake + (hudHpIcon_.basePosition.x - hudHpFrame_.basePosition.x) * damageImpact * 0.012f + hpIconShakeX * 0.25f,
             hudHpIcon_.basePosition.y + hpIconShakeY * 0.25f});
        hudHpIcon_.sprite->SetSize({hudHpIcon_.baseSize.x * hpIconPulse, hudHpIcon_.baseSize.y * hpIconPulse});
        hudHpIcon_.sprite->SetRotation(hpIconRotation);
        hudHpIcon_.sprite->SetColor({hudHpIcon_.baseColor.x, hudHpIcon_.baseColor.y, hudHpIcon_.baseColor.z,
                                     visible ? hudHpIcon_.baseColor.w : 0.0f});
        hudHpIcon_.sprite->Update();
    }

    if (hudHpDamageFill_.sprite) {
        const float rate = std::clamp(hudHpDelayedRate_, 0.0f, 1.0f);
        hudHpDamageFill_.sprite->SetVisible(visible && rate > 0.001f);
        hudHpDamageFill_.sprite->SetPosition(
            {hudHpDamageFill_.basePosition.x + liquidOffset.x, hudHpDamageFill_.basePosition.y + liquidOffset.y});
        hudHpDamageFill_.sprite->SetSize(
            {hudHpDamageFill_.baseSize.x * rate, hudHpDamageFill_.baseSize.y * liquidHeightScale});
        hudHpDamageFill_.sprite->SetColor(
            {hudHpDamageFill_.baseColor.x, hudHpDamageFill_.baseColor.y, hudHpDamageFill_.baseColor.z,
             visible ? hudHpDamageFill_.baseColor.w * (0.88f + damageRate * 0.12f) : 0.0f});
        hudHpDamageFill_.sprite->Update();
    }

    if (hudHpFill_.sprite) {
        hudHpFill_.sprite->SetVisible(visible && hpRate > 0.001f);
        hudHpFill_.sprite->SetPosition(
            {hudHpFill_.basePosition.x + liquidOffset.x, hudHpFill_.basePosition.y + liquidOffset.y});
        hudHpFill_.sprite->SetSize({hudHpFill_.baseSize.x * hpRate, hudHpFill_.baseSize.y * liquidHeightScale});
        hudHpFill_.sprite->SetColor(
            {1.0f, 1.0f - lowHpPulse * 0.08f, 1.0f - lowHpPulse * 0.14f, visible ? hudHpFill_.baseColor.w : 0.0f});
        hudHpFill_.sprite->Update();
    }

    if (hudHpHighlight_.sprite) {
        const float shimmer = 0.78f + hpRate * 0.10f + damageImpact * 0.12f;
        const float alpha = visible && hpRate > 0.001f ? hudHpHighlight_.baseColor.w * shimmer : 0.0f;
        hudHpHighlight_.sprite->SetVisible(visible && hpRate > 0.001f);
        hudHpHighlight_.sprite->SetPosition({hudHpHighlight_.basePosition.x + liquidOffset.x,
                                             hudHpHighlight_.basePosition.y - SpriteLayoutScaler::ScaleDesignY(0.35f)});
        hudHpHighlight_.sprite->SetSize(
            {hudHpHighlight_.baseSize.x * hpRate, hudHpHighlight_.baseSize.y * liquidHeightScale});
        hudHpHighlight_.sprite->SetColor(
            {hudHpHighlight_.baseColor.x, hudHpHighlight_.baseColor.y, hudHpHighlight_.baseColor.z, alpha});
        hudHpHighlight_.sprite->Update();
    }

    if (hudHpFrame_.sprite) {
        hudHpFrame_.sprite->SetVisible(visible);
        hudHpFrame_.sprite->SetPosition({hudHpFrame_.basePosition.x + barShake, hudHpFrame_.basePosition.y});
        hudHpFrame_.sprite->SetSize({hudHpFrame_.baseSize.x * (1.0f + damageImpact * 0.012f),
                                     hudHpFrame_.baseSize.y * (1.0f + damageImpact * 0.055f)});
        hudHpFrame_.sprite->SetColor(
            {1.0f, 1.0f - lowHpPulse * 0.10f, 1.0f - lowHpPulse * 0.16f, visible ? hudHpFrame_.baseColor.w : 0.0f});
        hudHpFrame_.sprite->Update();
    }

    // 残機とコインの取得反応を更新した後、数字の桁を描画状態へ反映します。
    if (hudLifeIcon_.sprite) {
        hudLifeIcon_.sprite->SetVisible(visible);
        hudLifeIcon_.sprite->SetPosition(hudLifeIcon_.basePosition);
        hudLifeIcon_.sprite->SetSize(
            {hudLifeIcon_.baseSize.x * lifeCountScaleX, hudLifeIcon_.baseSize.y * lifeCountScaleY});
        hudLifeIcon_.sprite->SetColor({hudLifeIcon_.baseColor.x, hudLifeIcon_.baseColor.y, hudLifeIcon_.baseColor.z,
                                       visible ? hudLifeIcon_.baseColor.w : 0.0f});
        hudLifeIcon_.sprite->Update();
    }

    if (hudLifeXIcon_.sprite) {
        hudLifeXIcon_.sprite->SetVisible(visible);
        hudLifeXIcon_.sprite->SetPosition(hudLifeXIcon_.basePosition);
        hudLifeXIcon_.sprite->SetSize({hudLifeXIcon_.baseSize.x * (1.0f + lifeGainRate * 0.08f),
                                       hudLifeXIcon_.baseSize.y * (1.0f + lifeGainRate * 0.08f)});
        hudLifeXIcon_.sprite->SetColor({hudLifeXIcon_.baseColor.x, hudLifeXIcon_.baseColor.y, hudLifeXIcon_.baseColor.z,
                                        visible ? hudLifeXIcon_.baseColor.w : 0.0f});
        hudLifeXIcon_.sprite->Update();
    }

    const engine::Vector2 lifeNumberRight = hudLifeDigits_[1].sprite ? hudLifeDigits_[1].basePosition : engine::Vector2{162.0f, 100.0f};
    const float lifeDigitHeight = hudLifeDigits_[1].baseSize.y > 0.0f ? hudLifeDigits_[1].baseSize.y : 40.0f;
    SetGameplayHUDNumber(hudLifeDigits_, lives, lifeNumberRight, lifeDigitHeight * (1.0f + lifeGainRate * 0.12f),
                         hudLifeDigits_[1].baseColor, visible);

    if (hudCoinIcon_.sprite) {
        hudCoinIcon_.sprite->SetVisible(visible);
        hudCoinIcon_.sprite->SetPosition(
            {hudCoinIcon_.basePosition.x, hudCoinIcon_.basePosition.y + coinBounceOffsetY});
        hudCoinIcon_.sprite->SetSize(
            {hudCoinIcon_.baseSize.x * coinPulseScaleX, hudCoinIcon_.baseSize.y * coinPulseScaleY});
        hudCoinIcon_.sprite->SetColor({hudCoinIcon_.baseColor.x, hudCoinIcon_.baseColor.y, hudCoinIcon_.baseColor.z,
                                       visible ? hudCoinIcon_.baseColor.w : 0.0f});
        hudCoinIcon_.sprite->Update();
    }

    if (hudCoinXIcon_.sprite) {
        hudCoinXIcon_.sprite->SetVisible(visible);
        hudCoinXIcon_.sprite->SetPosition(
            {hudCoinXIcon_.basePosition.x, hudCoinXIcon_.basePosition.y + coinBounceOffsetY * 0.55f});
        hudCoinXIcon_.sprite->SetSize(
            {hudCoinXIcon_.baseSize.x * coinCountScale, hudCoinXIcon_.baseSize.y * coinCountScale});
        hudCoinXIcon_.sprite->SetColor({hudCoinXIcon_.baseColor.x, hudCoinXIcon_.baseColor.y, hudCoinXIcon_.baseColor.z,
                                        visible ? hudCoinXIcon_.baseColor.w : 0.0f});
        hudCoinXIcon_.sprite->Update();
    }

    const engine::Vector2 coinNumberRight = hudCoinDigits_[1].sprite ? hudCoinDigits_[1].basePosition : engine::Vector2{162.0f, 154.0f};
    const float coinDigitHeight = hudCoinDigits_[1].baseSize.y > 0.0f ? hudCoinDigits_[1].baseSize.y : 40.0f;
    SetGameplayHUDNumber(hudCoinDigits_, coins, {coinNumberRight.x, coinNumberRight.y + coinBounceOffsetY * 0.55f},
                         coinDigitHeight * coinCountScale, hudCoinDigits_[1].baseColor, visible);
    UpdateStageStarHUD(deltaTime, visible);
    UpdatePrismBossHUD(deltaTime);
}

EnemyPrismSlime* GamePlayScene::FindPrismBossForHUD() {
    // 現シーンのプリズムボスを探し、HP表示の参照先にする。
    if (!objectManager_) {
        return nullptr;
    }

    for (const std::unique_ptr<engine::Object3d>& object : objectManager_->GetObjects()) {
        auto* boss = dynamic_cast<EnemyPrismSlime*>(object.get());

        if (boss && boss->IsEncounterHudActive()) {
            return boss;
        }
    }

    return nullptr;
}

EnemyMagmaSlime* GamePlayScene::FindMagmaBossForHUD() {
    // 現シーンのマグマボスを探し、HP表示の参照先にする。
    if (!objectManager_) {
        return nullptr;
    }

    for (const std::unique_ptr<engine::Object3d>& object : objectManager_->GetObjects()) {
        auto* boss = dynamic_cast<EnemyMagmaSlime*>(object.get());

        if (boss && boss->IsEncounterHudActive()) {
            return boss;
        }
    }

    return nullptr;
}

EnemyFalseKingSlime* GamePlayScene::FindFalseKingBossForHUD() {
    // 現シーンの偽王ボスを探し、HP表示の参照先にする。
    if (!objectManager_) {
        return nullptr;
    }

    for (const std::unique_ptr<engine::Object3d>& object : objectManager_->GetObjects()) {
        auto* boss = dynamic_cast<EnemyFalseKingSlime*>(object.get());

        if (boss && boss->IsEncounterHudActive()) {
            return boss;
        }
    }

    return nullptr;
}

// 戦闘中のボスを選び、登場・戦闘・退場の表示状態を同じ HUD で管理します。
void GamePlayScene::UpdatePrismBossHUD(float deltaTime) {
    constexpr float kIntroDuration = 1.62f;
    constexpr float kDismissDuration = 0.38f;
    deltaTime = (std::max)(0.0f, deltaTime);
    prismBossHudAnimationTimer_ = std::fmod(prismBossHudAnimationTimer_ + deltaTime, 1000.0f);

    // 候補の探索順はプリズム、マグマ、偽王。HUDは一度に一体だけ表示する。
    EnemyPrismSlime* prismBoss = FindPrismBossForHUD();
    EnemyMagmaSlime* magmaBoss = prismBoss ? nullptr : FindMagmaBossForHUD();
    EnemyFalseKingSlime* falseKingBoss = (prismBoss || magmaBoss) ? nullptr : FindFalseKingBossForHUD();
    const BossHudTheme requestedTheme =
        falseKingBoss ? BossHudTheme::FalseKing : (magmaBoss ? BossHudTheme::Magma : BossHudTheme::Prism);
    const bool usesMagmaTheme = requestedTheme == BossHudTheme::Magma;
    const bool usesFalseKingTheme = requestedTheme == BossHudTheme::FalseKing;
    const bool requestedVisible = prismBoss != nullptr || magmaBoss != nullptr || falseKingBoss != nullptr;
    const float maximumHp = prismBoss ? prismBoss->GetEncounterMaximumHp()
                                      : (magmaBoss ? magmaBoss->GetEncounterMaximumHp()
                                                   : (falseKingBoss ? falseKingBoss->GetEncounterMaximumHp() : 1.0f));
    const float currentHp =
        prismBoss
            ? (std::clamp)(prismBoss->GetEncounterCurrentHp(), 0.0f, maximumHp)
            : (magmaBoss
                   ? (std::clamp)(magmaBoss->GetEncounterCurrentHp(), 0.0f, maximumHp)
                   : (falseKingBoss ? (std::clamp)(falseKingBoss->GetEncounterCurrentHp(), 0.0f, maximumHp) : 0.0f));
    const float actualRate = (std::clamp)(currentHp / maximumHp, 0.0f, 1.0f);
    const int falseKingPhase = falseKingBoss ? (std::clamp)(falseKingBoss->GetBattlePhase(), 1, 3) : 1;
    const float falseKingPhaseTransition = falseKingBoss ? falseKingBoss->GetPhaseTransitionProgress() : 1.0f;

    // テーマが切り替わったときだけ名前と左右の装飾テクスチャを差し替える。
    if (requestedVisible && prismBossHudTheme_ != requestedTheme) {
        prismBossHudTheme_ = requestedTheme;
        const char* nameTexture = usesFalseKingTheme ? "ui/hud/boss/false_king_slime_name.png"
                                                     : (usesMagmaTheme ? "ui/hud/boss/magma_slime_name.png"
                                                                       : "ui/hud/boss/prism_slime_name.png");
        const char* sideDecorationTexture = usesMagmaTheme ? "particle/flame_soft.png" : "particle/diamond_shard.png";
        SetFullTextureRect(prismBossHudName_.sprite, engine::Sprite::LoadTexture(nameTexture));
        const uint32_t sideDecorationHandle = engine::Sprite::LoadTexture(sideDecorationTexture);
        SetFullTextureRect(prismBossHudShardLeft_.sprite, sideDecorationHandle);
        SetFullTextureRect(prismBossHudShardRight_.sprite, sideDecorationHandle);
    }

    // 出現・消滅を検知して表示状態を遷移させる。再出現時はHPアニメーションの基準も初期化する。
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
    } else if (!requestedVisible && prismBossHudPhase_ != PrismBossHudPhase::Hidden &&
               prismBossHudPhase_ != PrismBossHudPhase::Dismissing) {
        prismBossHudPhase_ = PrismBossHudPhase::Dismissing;
        prismBossHudTimer_ = 0.0f;
    }

    // 状態ごとに枠・名前・HP・全体透明度の進み方を分ける。
    float frameReveal = 0.0f;
    float nameReveal = 0.0f;
    float visualAlpha = 0.0f;

    switch (prismBossHudPhase_) {
    case PrismBossHudPhase::Hidden:
        break;

    case PrismBossHudPhase::Introducing: {
        // ボス本体の登場進捗も枠の開きに反映し、HPバーは名前より遅れて見せる。
        prismBossHudTimer_ += deltaTime;
        const float appearanceProgress =
            prismBoss ? prismBoss->GetEncounterAppearanceProgress()
                      : (magmaBoss ? magmaBoss->GetEncounterAppearanceProgress()
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

        // HP減少直後は旧HPの帯を短時間残し、その後に遅れて現在HPまで縮める。
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
        // 対象が消えたときは急に非表示にせず、全体の透明度を落とす。
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

    // 被弾とフェーズ移行の光り方は別タイマーで管理する。
    prismBossHudDamagePulseTimer_ = (std::max)(0.0f, prismBossHudDamagePulseTimer_ - deltaTime);
    falseKingBossHudPhasePulseTimer_ = (std::max)(0.0f, falseKingBossHudPhasePulseTimer_ - deltaTime);

    if (usesFalseKingTheme && falseKingPhase != falseKingBossHudPreviousPhase_) {
        falseKingBossHudPreviousPhase_ = falseKingPhase;
        falseKingBossHudPhasePulseTimer_ = 0.82f;
    }

    // 状態から描画に渡す共通の揺れ・拡大・低HP点滅の係数を作る。
    const bool hudVisible = visualAlpha > 0.001f && prismBossHudPhase_ != PrismBossHudPhase::Hidden;
    const float damagePulseRate = (std::clamp)(prismBossHudDamagePulseTimer_ / 0.44f, 0.0f, 1.0f);
    const float damageImpact = std::sin((1.0f - damagePulseRate) * kPi) * damagePulseRate;
    const float barShake = damagePulseRate > 0.0f ? std::sin(prismBossHudDamagePulseTimer_ * 96.0f) *
                                                        SpriteLayoutScaler::ScaleDesignX(4.0f) * damagePulseRate
                                                  : 0.0f;
    const float barScale = 0.12f + frameReveal * 0.88f;
    const float displayedRate = (std::clamp)(prismBossHudDisplayedRate_, 0.0f, 1.0f);
    const float delayedRate = (std::clamp)(prismBossHudDelayedRate_, displayedRate, 1.0f);
    const float lowHpRate = (std::clamp)((0.34f - displayedRate) / 0.34f, 0.0f, 1.0f);
    const float lowHpPulse = lowHpRate * (0.5f + 0.5f * std::sin(prismBossHudAnimationTimer_ * 7.0f));

    // 各部品の可視性・位置・大きさ・色を同じ手順で反映する。
    auto updateSprite = [](HudSpriteState& state, bool visible, const engine::Vector2& position, const engine::Vector2& size,
                           const engine::Vector4& color, float rotation = 0.0f) {
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

    // 三種類のボスと偽王の戦闘段階に応じて、同じ表示部品の配色を決める。
    const float phasePulseRate = (std::clamp)(falseKingBossHudPhasePulseTimer_ / 0.82f, 0.0f, 1.0f);
    const float phaseImpact = std::sin((1.0f - phasePulseRate) * kPi) * phasePulseRate;
    const float glowPulse = 0.82f + std::sin(prismBossHudAnimationTimer_ * 3.6f) * 0.18f + phaseImpact * 0.34f;
    const engine::Vector3 glowColor =
        usesFalseKingTheme
            ? (falseKingPhase >= 3 ? engine::Vector3{0.72f, 0.22f + phaseImpact * 0.28f, 1.0f}
                                   : (falseKingPhase == 2 ? engine::Vector3{1.0f, 0.28f + phaseImpact * 0.22f, 0.72f}
                                                          : engine::Vector3{1.0f, 0.54f + lowHpPulse * 0.22f, 0.18f}))
            : (usesMagmaTheme ? engine::Vector3{1.0f, 0.26f + lowHpPulse * 0.14f, 0.025f}
                              : engine::Vector3{0.34f + lowHpPulse * 0.30f, 0.58f, 1.0f});
    const engine::Vector3 fillColor =
        usesFalseKingTheme
            ? (falseKingPhase >= 3
                   ? engine::Vector3{0.72f + phaseImpact * 0.22f, 0.16f, 1.0f}
                   : (falseKingPhase == 2 ? engine::Vector3{1.0f, 0.34f, 0.72f + phaseImpact * 0.20f}
                                          : engine::Vector3{1.0f, 0.72f - lowHpPulse * 0.32f, 0.12f + lowHpPulse * 0.18f}))
            : (usesMagmaTheme ? engine::Vector3{1.0f, 0.48f - lowHpPulse * 0.30f, 0.035f}
                              : engine::Vector3{0.16f + lowHpPulse * 0.58f, 0.84f - lowHpPulse * 0.34f, 1.0f});
    const engine::Vector3 frameTopColor =
        usesFalseKingTheme
            ? (falseKingPhase >= 3 ? engine::Vector3{0.88f, 0.52f, 1.0f}
                                   : (falseKingPhase == 2 ? engine::Vector3{1.0f, 0.54f, 0.86f} : engine::Vector3{1.0f, 0.90f, 0.42f}))
            : (usesMagmaTheme ? engine::Vector3{1.0f, 0.88f, 0.32f} : engine::Vector3{0.54f + lowHpPulse * 0.30f, 0.94f, 1.0f});
    const engine::Vector3 frameBottomColor =
        usesFalseKingTheme
            ? (falseKingPhase >= 3 ? engine::Vector3{0.32f, 0.72f, 1.0f}
                                   : (falseKingPhase == 2 ? engine::Vector3{0.72f, 0.20f, 1.0f}
                                                          : engine::Vector3{0.62f + lowHpPulse * 0.20f, 0.22f, 0.92f}))
            : (usesMagmaTheme ? engine::Vector3{0.62f + lowHpPulse * 0.30f, 0.045f, 0.015f}
                              : engine::Vector3{0.66f + lowHpPulse * 0.24f, 0.44f, 1.0f});

    // 背景と枠は出現率で横幅を開き、被弾時だけ短く揺らす。
    updateSprite(prismBossHudGlow_, hudVisible,
                 {prismBossHudGlow_.basePosition.x + barShake, prismBossHudGlow_.basePosition.y},
                 {prismBossHudGlow_.baseSize.x * barScale * (1.0f + damageImpact * 0.025f),
                  prismBossHudGlow_.baseSize.y * (0.78f + frameReveal * 0.22f)},
                 {glowColor.x, glowColor.y, glowColor.z, prismBossHudGlow_.baseColor.w * visualAlpha * glowPulse});
    updateSprite(prismBossHudBack_, hudVisible,
                 {prismBossHudBack_.basePosition.x + barShake, prismBossHudBack_.basePosition.y},
                 {prismBossHudBack_.baseSize.x * barScale, prismBossHudBack_.baseSize.y},
                 {prismBossHudBack_.baseColor.x, prismBossHudBack_.baseColor.y, prismBossHudBack_.baseColor.z,
                  prismBossHudBack_.baseColor.w * visualAlpha});
    updateSprite(prismBossHudTrack_, hudVisible,
                 {prismBossHudTrack_.basePosition.x + barShake, prismBossHudTrack_.basePosition.y},
                 {prismBossHudTrack_.baseSize.x * barScale, prismBossHudTrack_.baseSize.y},
                 {prismBossHudTrack_.baseColor.x, prismBossHudTrack_.baseColor.y, prismBossHudTrack_.baseColor.z,
                  prismBossHudTrack_.baseColor.w * visualAlpha});

    // 現在HPと遅延帯は左端を共有し、それぞれの割合だけ横幅を縮める。
    const float fullFillWidth = prismBossHudFill_.baseSize.x * barScale;
    const float fullFillLeft =
        prismBossHudFill_.basePosition.x + (prismBossHudFill_.baseSize.x - fullFillWidth) * 0.5f + barShake;
    const float damageFillWidth = prismBossHudDamageFill_.baseSize.x * barScale;
    const float damageFillLeft = prismBossHudDamageFill_.basePosition.x +
                                 (prismBossHudDamageFill_.baseSize.x - damageFillWidth) * 0.5f + barShake;
    const float highlightWidth = prismBossHudHighlight_.baseSize.x * barScale;
    const float highlightLeft =
        prismBossHudHighlight_.basePosition.x + (prismBossHudHighlight_.baseSize.x - highlightWidth) * 0.5f + barShake;
    updateSprite(prismBossHudDamageFill_, hudVisible && delayedRate > 0.001f,
                 {damageFillLeft, prismBossHudDamageFill_.basePosition.y},
                 {damageFillWidth * delayedRate, prismBossHudDamageFill_.baseSize.y},
                 {1.0f, usesFalseKingTheme ? 0.28f : (usesMagmaTheme ? 0.10f : 0.24f + lowHpPulse * 0.12f),
                  usesFalseKingTheme ? 0.52f : (usesMagmaTheme ? 0.025f : 0.88f),
                  prismBossHudDamageFill_.baseColor.w * visualAlpha});
    updateSprite(prismBossHudFill_, hudVisible && displayedRate > 0.001f,
                 {fullFillLeft, prismBossHudFill_.basePosition.y},
                 {fullFillWidth * displayedRate, prismBossHudFill_.baseSize.y * (1.0f + damageImpact * 0.09f)},
                 {fillColor.x, fillColor.y, fillColor.z, prismBossHudFill_.baseColor.w * visualAlpha});
    updateSprite(prismBossHudHighlight_, hudVisible && displayedRate > 0.001f,
                 {highlightLeft, prismBossHudHighlight_.basePosition.y},
                 {highlightWidth * displayedRate, prismBossHudHighlight_.baseSize.y},
                 {1.0f, usesFalseKingTheme ? 0.96f : (usesMagmaTheme ? 0.86f : 1.0f),
                  usesFalseKingTheme ? 0.72f : (usesMagmaTheme ? 0.36f : 1.0f),
                  prismBossHudHighlight_.baseColor.w * visualAlpha * (0.74f + glowPulse * 0.26f)});
    updateSprite(prismBossHudFrameTop_, hudVisible,
                 {prismBossHudFrameTop_.basePosition.x + barShake, prismBossHudFrameTop_.basePosition.y},
                 {prismBossHudFrameTop_.baseSize.x * barScale, prismBossHudFrameTop_.baseSize.y},
                 {frameTopColor.x, frameTopColor.y, frameTopColor.z, prismBossHudFrameTop_.baseColor.w * visualAlpha});
    updateSprite(prismBossHudFrameBottom_, hudVisible,
                 {prismBossHudFrameBottom_.basePosition.x + barShake, prismBossHudFrameBottom_.basePosition.y},
                 {prismBossHudFrameBottom_.baseSize.x * barScale, prismBossHudFrameBottom_.baseSize.y},
                 {frameBottomColor.x, frameBottomColor.y, frameBottomColor.z,
                  prismBossHudFrameBottom_.baseColor.w * visualAlpha});

    // 左右装飾と名前は枠より遅れて現れ、被弾時だけ少し拡大する。
    const float shardScale = (0.55f + frameReveal * 0.45f) * (1.0f + damageImpact * 0.13f);
    const float shardAlpha = visualAlpha * frameReveal;
    updateSprite(prismBossHudShardLeft_, hudVisible,
                 {prismBossHudShardLeft_.basePosition.x + barShake, prismBossHudShardLeft_.basePosition.y},
                 {prismBossHudShardLeft_.baseSize.x * shardScale, prismBossHudShardLeft_.baseSize.y * shardScale},
                 {usesFalseKingTheme ? 1.0f : (usesMagmaTheme ? 1.0f : 0.42f + lowHpPulse * 0.35f),
                  usesFalseKingTheme ? 0.68f : (usesMagmaTheme ? 0.31f : 0.90f),
                  usesFalseKingTheme ? 0.18f : (usesMagmaTheme ? 0.025f : 1.0f),
                  prismBossHudShardLeft_.baseColor.w * shardAlpha},
                 -0.18f + std::sin(prismBossHudAnimationTimer_ * 2.8f) * 0.025f);
    updateSprite(prismBossHudShardRight_, hudVisible,
                 {prismBossHudShardRight_.basePosition.x + barShake, prismBossHudShardRight_.basePosition.y},
                 {prismBossHudShardRight_.baseSize.x * shardScale, prismBossHudShardRight_.baseSize.y * shardScale},
                 {usesFalseKingTheme ? 0.72f : (usesMagmaTheme ? 1.0f : 0.76f + lowHpPulse * 0.20f),
                  usesFalseKingTheme ? 0.24f : (usesMagmaTheme ? 0.66f : 0.42f),
                  usesFalseKingTheme ? 1.0f : (usesMagmaTheme ? 0.08f : 1.0f),
                  prismBossHudShardRight_.baseColor.w * shardAlpha},
                 0.18f - std::sin(prismBossHudAnimationTimer_ * 2.8f) * 0.025f);

    const float nameScale = 0.82f + nameReveal * 0.18f + damageImpact * 0.035f;
    updateSprite(prismBossHudName_, hudVisible && nameReveal > 0.001f,
                 {prismBossHudName_.basePosition.x + barShake * 0.45f,
                  prismBossHudName_.basePosition.y - SpriteLayoutScaler::ScaleDesignY((1.0f - nameReveal) * 11.0f)},
                 {prismBossHudName_.baseSize.x * nameScale, prismBossHudName_.baseSize.y * nameScale},
                 {1.0f, usesFalseKingTheme ? 0.96f : (usesMagmaTheme ? 0.96f : 1.0f),
                  usesFalseKingTheme ? 0.84f : (usesMagmaTheme ? 0.78f : 1.0f),
                  prismBossHudName_.baseColor.w * visualAlpha * nameReveal});

    // 偽王だけに段階境界と王冠を重ね、現在・通過済み・未到達を色と動きで区別する。
    const bool showFalseKingPhases = hudVisible && usesFalseKingTheme && frameReveal > 0.10f;

    for (HudSpriteState& divider : falseKingBossHudPhaseDividers_) {
        const float dividerGlow = 0.72f + std::sin(prismBossHudAnimationTimer_ * 4.2f) * 0.18f;
        updateSprite(divider, showFalseKingPhases, {divider.basePosition.x + barShake, divider.basePosition.y},
                     {divider.baseSize.x * (1.0f + phaseImpact * 0.42f), divider.baseSize.y * frameReveal},
                     {1.0f, falseKingPhase >= 3 ? 0.54f : 0.84f, falseKingPhase >= 3 ? 1.0f : 0.28f,
                      visualAlpha * frameReveal * dividerGlow});
    }

    for (size_t i = 0; i < falseKingBossHudPhaseCrowns_.size(); ++i) {
        HudSpriteState& crown = falseKingBossHudPhaseCrowns_[i];
        const int markerPhase = static_cast<int>(i) + 1;
        const bool currentPhase = markerPhase == falseKingPhase;
        const bool clearedPhase = markerPhase < falseKingPhase;
        const float markerPulse = currentPhase ? 0.5f + 0.5f * std::sin(prismBossHudAnimationTimer_ * 5.6f) : 0.0f;
        const float transitionPulse = currentPhase ? phaseImpact + (1.0f - falseKingPhaseTransition) * 0.12f : 0.0f;
        const float markerScale = (currentPhase ? 1.0f + markerPulse * 0.10f + transitionPulse * 0.26f : 0.76f) *
                                  (0.72f + nameReveal * 0.28f);
        engine::Vector4 markerColor;

        if (currentPhase) {
            markerColor = falseKingPhase >= 3
                              ? engine::Vector4{0.88f, 0.48f, 1.0f, visualAlpha * (0.86f + markerPulse * 0.14f)}
                              : engine::Vector4{1.0f, 0.82f, 0.24f, visualAlpha * (0.86f + markerPulse * 0.14f)};
        } else if (clearedPhase) {
            markerColor = {0.72f, 0.44f, 0.92f, visualAlpha * 0.54f};
        } else {
            markerColor = {0.22f, 0.10f, 0.34f, visualAlpha * 0.46f};
        }

        updateSprite(crown, showFalseKingPhases,
                     {crown.basePosition.x + barShake,
                      crown.basePosition.y - markerPulse * SpriteLayoutScaler::ScaleDesignY(2.5f)},
                     {crown.baseSize.x * markerScale, crown.baseSize.y * markerScale}, markerColor,
                     currentPhase ? std::sin(prismBossHudAnimationTimer_ * 4.2f) * 0.035f : 0.0f);
    }

    // 第二段階の詠唱中は残り時間に比例したゲージを表示する。
    const float castRemaining = falseKingBoss ? falseKingBoss->GetSealCastRemainingTime() : 0.0f;
    const bool showCast = showFalseKingPhases && falseKingPhase == 2 && castRemaining > 0.0f;
    const float castRate = showCast
                               ? (std::clamp)(castRemaining / CrownFinaleTuning::Get().sealCastTime, 0.0f, 1.0f)
                               : 0.0f;
    updateSprite(falseKingCastTrack_, showCast, falseKingCastTrack_.basePosition,
                 falseKingCastTrack_.baseSize, {0.20f, 0.07f, 0.30f, 0.82f * visualAlpha});
    updateSprite(falseKingCastFill_, showCast, falseKingCastFill_.basePosition,
                 {falseKingCastFill_.baseSize.x * castRate, falseKingCastFill_.baseSize.y},
                 {0.77f + (1.0f - castRate) * 0.22f, 0.29f, 1.0f, 0.96f * visualAlpha});

    // 最後の光点だけHPバーの現在値末端を追い、残りは固定位置で明滅させる。
    for (size_t i = 0; i < prismBossHudGlints_.size(); ++i) {
        HudSpriteState& glint = prismBossHudGlints_[i];
        const float phase =
            prismBossHudAnimationTimer_ * (3.2f + static_cast<float>(i) * 0.17f) + static_cast<float>(i) * 1.31f;
        const float sparkle = std::pow((std::max)(0.0f, std::sin(phase)), 7.0f);
        engine::Vector2 position = glint.basePosition;

        if (i == prismBossHudGlints_.size() - 1) {
            position.x = fullFillLeft + fullFillWidth * displayedRate;
            position.y = prismBossHudFill_.basePosition.y;
        }

        position.x += barShake;
        const float glintScale = 0.55f + sparkle * 1.15f + (i == prismBossHudGlints_.size() - 1 ? 0.30f : 0.0f);
        const float glintAlpha = visualAlpha * frameReveal * (0.08f + sparkle * 0.92f);
        updateSprite(glint, hudVisible && frameReveal > 0.1f, position,
                     {glint.baseSize.x * glintScale, glint.baseSize.y * glintScale},
                     {usesFalseKingTheme ? 1.0f : (usesMagmaTheme ? 1.0f : 0.72f + lowHpPulse * 0.20f),
                      usesFalseKingTheme ? 0.78f : (usesMagmaTheme ? 0.58f : 0.96f),
                      usesFalseKingTheme ? 0.28f : (usesMagmaTheme ? 0.12f : 1.0f), glint.baseColor.w * glintAlpha},
                     phase * 0.18f);
    }
}

void GamePlayScene::DrawGameplayHUD() {
    // 残機喪失中は専用演出へ描画を譲り、通常HUDを隠す。
    if (lifeLostState_.IsActive() || lifeLostState_.HoldsBlack()) {
        DrawLifeLostPresentation();
        return;
    }

    // 収集数、スター、ボス表示を先に描き、プレイヤーHPとコピー表示を上に重ねる。
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

    // HPは枠の上へ遅延帯・現在値・光を順に重ねる。
    DrawGameplayHUDSprite(hudHpFrame_);
    DrawGameplayHUDSprite(hudHpDamageFill_);
    DrawGameplayHUDSprite(hudHpFill_);
    DrawGameplayHUDSprite(hudHpHighlight_);
    DrawGameplayHUDSprite(hudHpIcon_);
    DrawGameplayHUDSprite(hudCopyStyle_);

    DrawLifeLostPresentation();
}

void GamePlayScene::DrawPrismBossHUD() {
    // UpdatePrismBossHUDで各Spriteの可視性と色を決め、ここでは重ね順だけ管理する。
    DrawGameplayHUDSprite(prismBossHudGlow_);
    DrawGameplayHUDSprite(prismBossHudBack_);
    DrawGameplayHUDSprite(prismBossHudTrack_);

    // 遅延ダメージ帯の上に現在HPを置き、光と枠で輪郭を作る。
    DrawGameplayHUDSprite(prismBossHudDamageFill_);
    DrawGameplayHUDSprite(prismBossHudFill_);
    DrawGameplayHUDSprite(prismBossHudHighlight_);
    DrawGameplayHUDSprite(prismBossHudFrameTop_);
    DrawGameplayHUDSprite(prismBossHudFrameBottom_);
    DrawGameplayHUDSprite(prismBossHudShardLeft_);
    DrawGameplayHUDSprite(prismBossHudShardRight_);
    DrawGameplayHUDSprite(prismBossHudName_);

    // 偽王専用の段階表示と詠唱表示は、共通の名前・HPバーより前面に置く。
    for (const HudSpriteState& divider : falseKingBossHudPhaseDividers_) {
        DrawGameplayHUDSprite(divider);
    }

    for (const HudSpriteState& crown : falseKingBossHudPhaseCrowns_) {
        DrawGameplayHUDSprite(crown);
    }

    DrawGameplayHUDSprite(falseKingCastTrack_);
    DrawGameplayHUDSprite(falseKingCastFill_);

    // 光点はすべての表示部品の最後に描き、ゲージ末端も隠れないようにする。
    for (const HudSpriteState& glint : prismBossHudGlints_) {
        DrawGameplayHUDSprite(glint);
    }
}

void GamePlayScene::DrawStageStarHUD() {
    // 取得済みの星と飛行演出を同じHUD層へ描く。
    for (const auto& slot : hudStageStarSlots_) {
        DrawGameplayHUDSprite(slot);
    }

    for (const auto& particle : hudStageStarFlyParticles_) {
        if (particle.sprite) {
            particle.sprite->Draw();
        }
    }
}

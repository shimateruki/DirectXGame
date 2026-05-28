#define NOMINMAX
#include "GameClearScene.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "AudioPlayer.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "ParticleSystem.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include "LightManager.h"
#include "SceneManager.h"
#include "DebugConsole.h"
#include "BulletManager.h"
#include "LevelLoader.h"
#include "GameRule.h"
#include "CameraEditor.h"
#include "LightEditor.h"
#include "ParticleManager.h"
#include "GPUParticleManager.h"
#include <SaveDataManager.h>
#include "PlayerState.h"
#include "PostEffect.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr const char* kClearVictoryStarBurstPreset = "ClearVictoryStarBurst";
constexpr const char* kClearVictorySparklePreset = "ClearVictorySparkle";
constexpr const char* kClearVictoryFloorTwinklePreset = "ClearVictoryFloorTwinkle";
constexpr float kResultMainTimeScale = 1.08f;
constexpr float kResultMainTimeSpacing = 1.16f;
constexpr float kResultDiffTimeScale = 0.42f;
constexpr float kResultTitleGlyphScale = 1.55f;
constexpr float kResultLabelGlyphScale = 2.0f;
constexpr float kResultTitleExclamationHeight = 64.0f;
constexpr float kResultLabelGlyphHeight = 52.0f;

struct ClearTitleGlyphSource {
    const char* textureName;
    Vector2 sourceLeftTop;
    Vector2 sourceSize;
    float targetHeight = 0.0f;
};

constexpr std::array<ClearTitleGlyphSource, 7> kClearTitleGlyphSources = {
    ClearTitleGlyphSource{ "UI/TextGe.png", { 41.0f, 36.0f }, { 57.0f, 54.0f } },
    ClearTitleGlyphSource{ "UI/Text-.png",  { 44.0f, 62.0f }, { 50.0f, 7.0f } },
    ClearTitleGlyphSource{ "UI/TextMu.png", { 43.0f, 41.0f }, { 53.0f, 48.0f } },
    ClearTitleGlyphSource{ "UI/TextKu.png", { 45.0f, 39.0f }, { 45.0f, 51.0f } },
    ClearTitleGlyphSource{ "UI/TextRi.png", { 50.0f, 42.0f }, { 38.0f, 49.0f } },
    ClearTitleGlyphSource{ "UI/TextA.png",  { 46.0f, 43.0f }, { 49.0f, 47.0f } },
    ClearTitleGlyphSource{ "UI/Text!.png",  { 46.0f, 43.0f }, { 9.0f, 44.0f }, kResultTitleExclamationHeight },
};

constexpr std::array<ClearTitleGlyphSource, 5> kPlayerTimeGlyphSources = {
    ClearTitleGlyphSource{ "UI/Textzi.png", { 43.0f, 32.0f }, { 32.0f, 38.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/Textko.png", { 44.0f, 35.0f }, { 33.0f, 34.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/Textta.png", { 43.0f, 34.0f }, { 31.0f, 34.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/Texti.png",  { 42.0f, 36.0f }, { 34.0f, 32.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/TextMu.png", { 43.0f, 41.0f }, { 53.0f, 48.0f }, kResultLabelGlyphHeight },
};

constexpr std::array<ClearTitleGlyphSource, 6> kBestTimeGlyphSources = {
    ClearTitleGlyphSource{ "UI/Textbe.png", { 40.0f, 34.0f }, { 38.0f, 31.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/Textsu.png", { 43.0f, 37.0f }, { 32.0f, 31.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/Textto.png", { 48.0f, 35.0f }, { 27.0f, 33.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/Textta.png", { 43.0f, 34.0f }, { 31.0f, 34.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/Texti.png",  { 42.0f, 36.0f }, { 34.0f, 32.0f }, kResultLabelGlyphHeight },
    ClearTitleGlyphSource{ "UI/TextMu.png", { 43.0f, 41.0f }, { 53.0f, 48.0f }, kResultLabelGlyphHeight },
};

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseOutBack(float t) {
    t = Clamp01(t);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

float EaseOutCubic(float t) {
    t = Clamp01(t);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float ComputeResultPop(float timer, float duration, float amount) {
    if (timer < 0.0f || timer > duration) {
        return 0.0f;
    }

    const float t = Clamp01(timer / duration);
    return amount * std::sin(t * 3.14159265f) * (1.0f - 0.18f * t);
}

Vector2 ResolveGlyphSize(const ClearTitleGlyphSource& source, float defaultScale) {
    const float scale = source.targetHeight > 0.0f
        ? source.targetHeight / source.sourceSize.y
        : defaultScale;
    return {
        source.sourceSize.x * scale,
        source.sourceSize.y * scale
    };
}

Vector4 WithAlpha(Vector4 color, float alpha) {
    color.w = Clamp01(alpha);
    return color;
}
}

void GameClearScene::SetSpriteTexturePreserveSize(Sprite* sprite, const std::string& textureName) {
    if (!sprite || sprite->GetTextureName() == textureName) {
        return;
    }

    Vector2 currentSize = sprite->GetSize();
    sprite->SetTextureHandle(Sprite::LoadTexture(textureName));
    sprite->SetTextureName(textureName);
    sprite->SetSize(currentSize);
}

void GameClearScene::ApplyInputUiIfNeeded() {
    const bool useGamepadUi = inputManager_ && inputManager_->IsGamepadMode();
    if (hasAppliedClearInputUi_ && clearUiUsesGamepad_ == useGamepadUi) {
        return;
    }

    SetSpriteTexturePreserveSize(
        enterTextSprite_,
        useGamepadUi ? "enter_text_pad.png" : "enter_text.png");

    clearUiUsesGamepad_ = useGamepadUi;
    hasAppliedClearInputUi_ = true;

    DebugConsole::GetInstance()->AddLog(
        useGamepadUi
        ? "[GameClearUI] Input display switched to Controller"
        : "[GameClearUI] Input display switched to Keyboard");
}

void GameClearScene::ResetVictoryPoseParticles() {
    victoryParticleBurstEmitted_ = false;
    victoryParticleTimer_ = 0.0f;
}

void GameClearScene::EmitVictoryParticle(const std::string& presetName, const Vector3& offset) {
    if (!player_) {
        return;
    }

    Vector3 position = player_->GetWorldPosition();
    position.x += offset.x;
    position.y += offset.y;
    position.z += offset.z;
    GPUParticleManager::GetInstance()->Emit(presetName, position);
}

void GameClearScene::UpdateVictoryPoseParticles(float deltaTime) {
    bool isVictoryPoseActive =
        clearState_ == ClearState::kVictoryMotion ||
        clearState_ == ClearState::kShowClearTime ||
        clearState_ == ClearState::kShowBestTime ||
        clearState_ == ClearState::kWaitInput;

    if (!isVictoryPoseActive || !player_) {
        return;
    }

    victoryParticleTimer_ += deltaTime;

    if (!victoryParticleBurstEmitted_ && victoryParticleTimer_ >= 0.28f) {
        EmitVictoryParticle(kClearVictoryStarBurstPreset, { 0.0f, 2.0f, 0.0f });
        EmitVictoryParticle(kClearVictorySparklePreset, { 0.0f, 2.4f, 0.0f });
        EmitVictoryParticle(kClearVictorySparklePreset, { 0.9f, 1.45f, 0.35f });
        EmitVictoryParticle(kClearVictorySparklePreset, { -0.9f, 1.45f, -0.35f });
        EmitVictoryParticle(kClearVictorySparklePreset, { 0.0f, 1.45f, 0.9f });
        EmitVictoryParticle(kClearVictoryFloorTwinklePreset, { 0.0f, 0.35f, 0.0f });
        victoryParticleBurstEmitted_ = true;
    }
}

std::unique_ptr<Sprite> GameClearScene::CreateUiSprite(const Vector2& position, const Vector2& size, const Vector4& color) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteCommon_.get(), Sprite::LoadTexture("white.png"));
    sprite->SetPosition(position);
    sprite->SetSize(size);
    sprite->SetColor(color);
    sprite->Update();
    return sprite;
}

void GameClearScene::InitializeTextStrip(ResultTextStrip& strip, Sprite* sourceSprite, int pieceCount) {
    if (!sourceSprite || !spriteCommon_ || pieceCount <= 0) {
        return;
    }

    strip.pieces.clear();
    strip.basePosition = sourceSprite->GetPosition();
    strip.baseSize = sourceSprite->GetSize();
    strip.pieceCount = pieceCount;
    strip.animationTimer = 0.0f;
    strip.initialized = true;

    const uint32_t textureHandle = sourceSprite->GetTextureHandle();
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle);
    strip.sourceTextureSize = {
        static_cast<float>(metadata.width),
        static_cast<float>(metadata.height)
    };

    const float sourcePieceWidth = strip.sourceTextureSize.x / static_cast<float>(pieceCount);
    const float displayPieceWidth = strip.baseSize.x / static_cast<float>(pieceCount);
    strip.pieces.reserve(pieceCount);

    for (int i = 0; i < pieceCount; ++i) {
        auto piece = std::make_unique<Sprite>();
        piece->Initialize(spriteCommon_.get(), textureHandle);
        piece->SetTextureName(sourceSprite->GetTextureName());
        piece->SetTextureRect(
            { sourcePieceWidth * static_cast<float>(i), 0.0f },
            { sourcePieceWidth, strip.sourceTextureSize.y });
        piece->SetSize({ displayPieceWidth, strip.baseSize.y });
        piece->SetPosition(strip.basePosition);
        piece->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        piece->Update();
        strip.pieces.push_back(std::move(piece));
    }

    sourceSprite->SetVisible(false);
}

void GameClearScene::ResetTextStrip(ResultTextStrip& strip) {
    strip.animationTimer = 0.0f;
    for (auto& piece : strip.pieces) {
        if (!piece) {
            continue;
        }
        piece->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        piece->Update();
    }
}

void GameClearScene::UpdateTextStrip(ResultTextStrip& strip, float deltaTime, const Vector4& color, float scale, float idleAmount) {
    if (!strip.initialized || strip.pieceCount <= 0) {
        return;
    }

    const float alpha = Clamp01(color.w);
    if (alpha <= 0.001f) {
        for (auto& piece : strip.pieces) {
            if (piece) {
                Vector4 hiddenColor = color;
                hiddenColor.w = 0.0f;
                piece->SetColor(hiddenColor);
                piece->Update();
            }
        }
        return;
    }

    strip.animationTimer += deltaTime;

    const float scaledWidth = strip.baseSize.x * scale;
    const float pieceWidth = scaledWidth / static_cast<float>(strip.pieceCount);
    const float middleIndex = (static_cast<float>(strip.pieceCount) - 1.0f) * 0.5f;

    for (int i = 0; i < strip.pieceCount; ++i) {
        if (i >= static_cast<int>(strip.pieces.size()) || !strip.pieces[i]) {
            continue;
        }

        const float localTime = strip.animationTimer - strip.stepDelay * static_cast<float>(i);
        const float appearRate = Clamp01(localTime / strip.popDuration);
        const float easedAppear = EaseOutBack(appearRate);
        const float fadeRate = Clamp01(localTime / 0.16f);
        const float settle = std::sin(appearRate * 3.14159265f) * (1.0f - 0.25f * appearRate);
        const float entrySway = std::sin(strip.animationTimer * 9.0f + static_cast<float>(i) * 0.72f) * idleAmount * fadeRate * (1.0f - appearRate);
        const float pieceScale = scale * (0.76f + 0.24f * easedAppear + 0.08f * settle);
        const float centerOffsetX = (-scaledWidth * 0.5f) + pieceWidth * (static_cast<float>(i) + 0.5f);
        const float gatherOffsetX = (middleIndex - static_cast<float>(i)) * 12.0f * (1.0f - appearRate);

        strip.pieces[i]->SetPosition({
            strip.basePosition.x + centerOffsetX + gatherOffsetX,
            strip.basePosition.y - 24.0f * (1.0f - EaseOutCubic(appearRate)) - 7.0f * settle + entrySway
        });
        strip.pieces[i]->SetSize({
            (strip.baseSize.x / static_cast<float>(strip.pieceCount)) * pieceScale,
            strip.baseSize.y * pieceScale
        });

        Vector4 pieceColor = color;
        pieceColor.w = alpha * fadeRate;
        strip.pieces[i]->SetColor(pieceColor);
        strip.pieces[i]->Update();
    }
}

void GameClearScene::DrawTextStrip(const ResultTextStrip& strip) {
    for (const auto& piece : strip.pieces) {
        if (piece) {
            piece->Draw();
        }
    }
}

void GameClearScene::InitializeClearTitleGlyphStrip() {
    clearTitleGlyphStrip_.glyphs.clear();
    clearTitleGlyphStrip_.baseOffsets.clear();
    clearTitleGlyphStrip_.baseSizes.clear();
    clearTitleGlyphStrip_.basePosition = { 1120.0f, 104.0f };
    clearTitleGlyphStrip_.animationTimer = 0.0f;
    clearTitleGlyphStrip_.idleWaveEnabled = true;
    clearTitleGlyphStrip_.idleWaveStartDelay = 1.2f;
    clearTitleGlyphStrip_.idleWaveInterval = 2.35f;
    clearTitleGlyphStrip_.idleWaveStepDelay = 0.08f;
    clearTitleGlyphStrip_.idleWaveDuration = 0.34f;
    clearTitleGlyphStrip_.initialized = false;

    if (!spriteCommon_) {
        return;
    }

    constexpr float spacing = 16.0f;
    float totalWidth = spacing * static_cast<float>(kClearTitleGlyphSources.size() - 1);
    for (const auto& source : kClearTitleGlyphSources) {
        totalWidth += ResolveGlyphSize(source, kResultTitleGlyphScale).x;
    }

    float currentX = -totalWidth * 0.5f;
    for (const auto& source : kClearTitleGlyphSources) {
        const Vector2 glyphSize = ResolveGlyphSize(source, kResultTitleGlyphScale);

        auto glyph = std::make_unique<Sprite>();
        glyph->Initialize(spriteCommon_.get(), Sprite::LoadTexture(source.textureName));
        glyph->SetTextureName(source.textureName);
        glyph->SetTextureRect(source.sourceLeftTop, source.sourceSize);
        glyph->SetSize(glyphSize);
        glyph->SetPosition(clearTitleGlyphStrip_.basePosition);
        glyph->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        glyph->Update();

        clearTitleGlyphStrip_.baseOffsets.push_back({
            currentX + glyphSize.x * 0.5f,
            0.0f
        });
        clearTitleGlyphStrip_.baseSizes.push_back(glyphSize);
        clearTitleGlyphStrip_.glyphs.push_back(std::move(glyph));

        currentX += glyphSize.x + spacing;
    }

    clearTitleGlyphStrip_.initialized = true;
}

void GameClearScene::InitializePlayerTimeGlyphStrip() {
    playerTimeGlyphStrip_.glyphs.clear();
    playerTimeGlyphStrip_.baseOffsets.clear();
    playerTimeGlyphStrip_.baseSizes.clear();
    playerTimeGlyphStrip_.basePosition = { 1110.0f, 238.0f };
    playerTimeGlyphStrip_.animationTimer = 0.0f;
    playerTimeGlyphStrip_.stepDelay = 0.075f;
    playerTimeGlyphStrip_.popDuration = 0.38f;
    playerTimeGlyphStrip_.initialized = false;

    if (!spriteCommon_) {
        return;
    }

    constexpr float spacing = 12.0f;
    float totalWidth = spacing * static_cast<float>(kPlayerTimeGlyphSources.size() - 1);
    for (const auto& source : kPlayerTimeGlyphSources) {
        totalWidth += ResolveGlyphSize(source, kResultLabelGlyphScale).x;
    }

    float currentX = -totalWidth * 0.5f;
    for (const auto& source : kPlayerTimeGlyphSources) {
        const Vector2 glyphSize = ResolveGlyphSize(source, kResultLabelGlyphScale);

        auto glyph = std::make_unique<Sprite>();
        glyph->Initialize(spriteCommon_.get(), Sprite::LoadTexture(source.textureName));
        glyph->SetTextureName(source.textureName);
        glyph->SetTextureRect(source.sourceLeftTop, source.sourceSize);
        glyph->SetSize(glyphSize);
        glyph->SetPosition(playerTimeGlyphStrip_.basePosition);
        glyph->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        glyph->Update();

        playerTimeGlyphStrip_.baseOffsets.push_back({
            currentX + glyphSize.x * 0.5f,
            0.0f
        });
        playerTimeGlyphStrip_.baseSizes.push_back(glyphSize);
        playerTimeGlyphStrip_.glyphs.push_back(std::move(glyph));

        currentX += glyphSize.x + spacing;
    }

    playerTimeGlyphStrip_.initialized = true;
}

void GameClearScene::InitializeBestTimeGlyphStrip() {
    bestTimeGlyphStrip_.glyphs.clear();
    bestTimeGlyphStrip_.baseOffsets.clear();
    bestTimeGlyphStrip_.baseSizes.clear();
    bestTimeGlyphStrip_.basePosition = { 1110.0f, 498.0f };
    bestTimeGlyphStrip_.animationTimer = 0.0f;
    bestTimeGlyphStrip_.stepDelay = 0.075f;
    bestTimeGlyphStrip_.popDuration = 0.38f;
    bestTimeGlyphStrip_.initialized = false;

    if (!spriteCommon_) {
        return;
    }

    constexpr float spacing = 10.0f;
    float totalWidth = spacing * static_cast<float>(kBestTimeGlyphSources.size() - 1);
    for (const auto& source : kBestTimeGlyphSources) {
        totalWidth += ResolveGlyphSize(source, kResultLabelGlyphScale).x;
    }

    float currentX = -totalWidth * 0.5f;
    for (const auto& source : kBestTimeGlyphSources) {
        const Vector2 glyphSize = ResolveGlyphSize(source, kResultLabelGlyphScale);

        auto glyph = std::make_unique<Sprite>();
        glyph->Initialize(spriteCommon_.get(), Sprite::LoadTexture(source.textureName));
        glyph->SetTextureName(source.textureName);
        glyph->SetTextureRect(source.sourceLeftTop, source.sourceSize);
        glyph->SetSize(glyphSize);
        glyph->SetPosition(bestTimeGlyphStrip_.basePosition);
        glyph->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        glyph->Update();

        bestTimeGlyphStrip_.baseOffsets.push_back({
            currentX + glyphSize.x * 0.5f,
            0.0f
        });
        bestTimeGlyphStrip_.baseSizes.push_back(glyphSize);
        bestTimeGlyphStrip_.glyphs.push_back(std::move(glyph));

        currentX += glyphSize.x + spacing;
    }

    bestTimeGlyphStrip_.initialized = true;
}

void GameClearScene::ResetGlyphStrip(ResultGlyphStrip& strip) {
    strip.animationTimer = 0.0f;
    for (auto& glyph : strip.glyphs) {
        if (!glyph) {
            continue;
        }
        glyph->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
        glyph->Update();
    }
}

void GameClearScene::UpdateGlyphStrip(ResultGlyphStrip& strip, float deltaTime, const Vector4& color, float scale) {
    if (!strip.initialized) {
        return;
    }

    const float alpha = Clamp01(color.w);
    if (alpha <= 0.001f) {
        for (auto& glyph : strip.glyphs) {
            if (glyph) {
                Vector4 hiddenColor = color;
                hiddenColor.w = 0.0f;
                glyph->SetColor(hiddenColor);
                glyph->Update();
            }
        }
        return;
    }

    strip.animationTimer += deltaTime;

    for (int i = 0; i < static_cast<int>(strip.glyphs.size()); ++i) {
        if (!strip.glyphs[i] ||
            i >= static_cast<int>(strip.baseOffsets.size()) ||
            i >= static_cast<int>(strip.baseSizes.size())) {
            continue;
        }

        const float localTime = strip.animationTimer - strip.stepDelay * static_cast<float>(i);
        const float appearRate = Clamp01(localTime / strip.popDuration);
        const float fadeRate = Clamp01(localTime / 0.16f);
        const float easedAppear = EaseOutBack(appearRate);
        const float settle = std::sin(appearRate * 3.14159265f) * (1.0f - 0.28f * appearRate);
        const float direction = (i % 2 == 0) ? -1.0f : 1.0f;
        const float settleRate = EaseOutCubic(appearRate);
        const float entryOffsetX = direction * 18.0f * (1.0f - settleRate);
        const float entryOffsetY = -66.0f * (1.0f - settleRate) - 7.0f * settle;
        const float entryRotation = direction * 0.12f * (1.0f - settleRate);
        const float flash = std::sin(appearRate * 3.14159265f) * (1.0f - appearRate);
        float idleBounce = 0.0f;
        if (strip.idleWaveEnabled && appearRate >= 1.0f && !strip.glyphs.empty()) {
            const float waveStartTime =
                strip.stepDelay * static_cast<float>(strip.glyphs.size() - 1) +
                strip.popDuration +
                strip.idleWaveStartDelay;
            const float waveTimer = strip.animationTimer - waveStartTime;
            if (waveTimer >= 0.0f && strip.idleWaveInterval > 0.0f) {
                const float waveCycle = std::fmod(waveTimer, strip.idleWaveInterval);
                const float waveLocalTime = waveCycle - strip.idleWaveStepDelay * static_cast<float>(i);
                if (waveLocalTime >= 0.0f && waveLocalTime <= strip.idleWaveDuration) {
                    const float waveRate = Clamp01(waveLocalTime / strip.idleWaveDuration);
                    idleBounce = std::sin(waveRate * 3.14159265f);
                }
            }
        }
        const float glyphScale = scale * (0.58f + 0.42f * easedAppear + 0.12f * settle);
        const float waveScale = 1.0f + 0.07f * idleBounce;
        const float waveOffsetY = -12.0f * idleBounce;
        const float waveRotation = direction * 0.04f * idleBounce;
        const Vector2& offset = strip.baseOffsets[i];
        const Vector2& baseSize = strip.baseSizes[i];

        strip.glyphs[i]->SetPosition({
            strip.basePosition.x + offset.x * scale + entryOffsetX,
            strip.basePosition.y + offset.y + entryOffsetY + waveOffsetY
        });
        strip.glyphs[i]->SetSize({
            baseSize.x * glyphScale * waveScale,
            baseSize.y * glyphScale * waveScale
        });
        strip.glyphs[i]->SetRotation(entryRotation + waveRotation);

        Vector4 glyphColor = color;
        glyphColor.x = std::min(1.0f, glyphColor.x + 0.22f * flash);
        glyphColor.y = std::min(1.0f, glyphColor.y + 0.18f * flash);
        glyphColor.z = std::min(1.0f, glyphColor.z + 0.08f * flash);
        glyphColor.w = alpha * fadeRate;
        strip.glyphs[i]->SetColor(glyphColor);
        strip.glyphs[i]->Update();
    }
}

void GameClearScene::DrawGlyphStrip(const ResultGlyphStrip& strip) {
    for (const auto& glyph : strip.glyphs) {
        if (glyph) {
            glyph->Draw();
        }
    }
}

void GameClearScene::InitializeResultUiSprites() {
    resultPanelSprite_ = CreateUiSprite({ 1185.0f, 420.0f }, { 710.0f, 650.0f }, { 0.02f, 0.025f, 0.035f, 0.0f });
    resultPanelTopLineSprite_ = CreateUiSprite({ 1130.0f, 158.0f }, { 620.0f, 4.0f }, { 0.65f, 0.95f, 1.0f, 0.0f });
    resultPanelBottomLineSprite_ = CreateUiSprite({ 1185.0f, 745.0f }, { 500.0f, 3.0f }, { 0.65f, 0.95f, 1.0f, 0.0f });
    bestHighlightSprite_ = CreateUiSprite({ 1195.0f, 610.0f }, { 650.0f, 146.0f }, { 1.0f, 0.72f, 0.12f, 0.0f });
    bestHighlightTopLineSprite_ = CreateUiSprite({ 1195.0f, 538.0f }, { 590.0f, 5.0f }, { 1.0f, 0.86f, 0.28f, 0.0f });
    bestHighlightBottomLineSprite_ = CreateUiSprite({ 1195.0f, 682.0f }, { 590.0f, 5.0f }, { 1.0f, 0.86f, 0.28f, 0.0f });
    diffSignHorizontalSprite_ = CreateUiSprite({ 1310.0f, 610.0f }, { 20.0f, 4.0f }, { 1.0f, 0.86f, 0.55f, 0.0f });
    diffSignVerticalSprite_ = CreateUiSprite({ 1310.0f, 610.0f }, { 4.0f, 20.0f }, { 1.0f, 0.86f, 0.55f, 0.0f });
}

void GameClearScene::UpdateResultUiVisuals(float deltaTime) {
    const float titlePop = (clearState_ == ClearState::kVictoryMotion)
        ? 0.12f * (1.0f - Clamp01(stateTimer_ / 0.55f)) * EaseOutBack(Clamp01(stateTimer_ / 0.28f))
        : 0.0f;

    UpdateGlyphStrip(
        clearTitleGlyphStrip_,
        deltaTime,
        { 1.0f, 1.0f, 1.0f, resultAlpha_ },
        1.0f + titlePop);

    UpdateGlyphStrip(
        playerTimeGlyphStrip_,
        deltaTime,
        { 1.0f, 1.0f, 0.96f, clearTimeAlpha_ },
        1.0f);

    const float newBestPulse = isNewBest_
        ? 0.5f + 0.5f * std::sin(stateTimer_ * 7.5f)
        : 0.0f;
    const Vector4 bestColor = isNewBest_ && newBestAlpha_ > 0.0f
        ? Vector4{ 1.0f, 0.82f + 0.12f * newBestPulse, 0.22f, bestTimeAlpha_ }
        : Vector4{ 1.0f, 1.0f, 1.0f, bestTimeAlpha_ };

    UpdateGlyphStrip(
        bestTimeGlyphStrip_,
        deltaTime,
        bestColor,
        1.0f + (isNewBest_ ? 0.02f * newBestAlpha_ * newBestPulse : 0.0f));

    if (clearTimeUI_) {
        const float pop = ComputeResultPop(clearTimePopTimer_, 0.42f, 0.16f);
        clearTimeUI_->SetScale(kResultMainTimeScale + pop);
        clearTimeUI_->SetColor({ 1.0f, 1.0f, 0.96f, clearTimeAlpha_ });
    }

    if (bestTimeUI_) {
        const float pop = bestTimePopTimer_ > 0.0f
            ? 0.08f * (1.0f - Clamp01(bestTimePopTimer_ / 0.24f))
            : 0.0f;
        const float newBestScale = isNewBest_ ? 0.03f * newBestAlpha_ * newBestPulse : 0.0f;
        bestTimeUI_->SetScale(kResultMainTimeScale + pop + newBestScale);
        bestTimeUI_->SetColor(bestColor);
    }

    if (diffTimeUI_) {
        const Vector4 diffColor = diffIsPositive_
            ? Vector4{ 1.0f, 0.88f, 0.62f, diffAlpha_ * 0.82f }
            : Vector4{ 0.56f, 1.0f, 0.84f, diffAlpha_ * 0.9f };
        diffTimeUI_->SetScale(kResultDiffTimeScale);
        diffTimeUI_->SetColor(diffColor);
    }

    const float panelAlpha = resultPanelAlpha_;
    if (resultPanelSprite_) resultPanelSprite_->SetColor({ 0.015f, 0.018f, 0.026f, 0.28f * panelAlpha });
    if (resultPanelTopLineSprite_) resultPanelTopLineSprite_->SetColor({ 0.65f, 0.95f, 1.0f, 0.32f * panelAlpha });
    if (resultPanelBottomLineSprite_) resultPanelBottomLineSprite_->SetColor({ 0.65f, 0.95f, 1.0f, 0.12f * panelAlpha });

    const float highlightAlpha = newBestAlpha_ * bestTimeAlpha_;
    if (bestHighlightSprite_) bestHighlightSprite_->SetColor({ 1.0f, 0.72f, 0.12f, 0.12f * highlightAlpha });
    if (bestHighlightTopLineSprite_) bestHighlightTopLineSprite_->SetColor({ 1.0f, 0.86f, 0.28f, 0.62f * highlightAlpha });
    if (bestHighlightBottomLineSprite_) bestHighlightBottomLineSprite_->SetColor({ 1.0f, 0.86f, 0.28f, 0.42f * highlightAlpha });

    const Vector4 signColor = diffIsPositive_
        ? Vector4{ 1.0f, 0.86f, 0.55f, diffAlpha_ * 0.82f }
        : Vector4{ 0.56f, 1.0f, 0.84f, diffAlpha_ * 0.9f };
    if (diffSignHorizontalSprite_) diffSignHorizontalSprite_->SetColor(signColor);
    if (diffSignVerticalSprite_) diffSignVerticalSprite_->SetColor(WithAlpha(signColor, diffIsPositive_ ? diffAlpha_ : 0.0f));

    if (enterTextSprite_) {
        const float blink = 0.5f + 0.5f * std::sin(stateTimer_ * 5.5f);
        const float alphaPulse = 0.45f + 0.55f * blink;
        const float sizePulse = 1.0f + 0.035f * blink;
        enterTextSprite_->SetPosition(enterTextBasePosition_);
        enterTextSprite_->SetSize({
            enterTextBaseSize_.x * sizePulse,
            enterTextBaseSize_.y * sizePulse
        });
        enterTextSprite_->SetColor({ 1.0f, 1.0f, 1.0f, inputGuideAlpha_ * alphaPulse });
    }

    const auto updateSprite = [](const std::unique_ptr<Sprite>& sprite) {
        if (sprite) {
            sprite->Update();
        }
    };
    updateSprite(resultPanelSprite_);
    updateSprite(resultPanelTopLineSprite_);
    updateSprite(resultPanelBottomLineSprite_);
    updateSprite(bestHighlightSprite_);
    updateSprite(bestHighlightTopLineSprite_);
    updateSprite(bestHighlightBottomLineSprite_);
    updateSprite(diffSignHorizontalSprite_);
    updateSprite(diffSignVerticalSprite_);
}

void GameClearScene::PreviewNewBestEffect() {
    isNewBest_ = true;
    diffIsPositive_ = false;
    clearState_ = ClearState::kShowBestTime;
    stateTimer_ = 0.0f;

    resultPanelAlpha_ = 1.0f;
    resultAlpha_ = 1.0f;
    clearTimeAlpha_ = 1.0f;
    bestTimeAlpha_ = 0.0f;
    diffAlpha_ = 0.0f;
    inputGuideAlpha_ = 0.0f;
    menuAlpha_ = 0.0f;
    newBestAlpha_ = 0.0f;
    clearTimePopTimer_ = 1.0f;
    bestTimePopTimer_ = -1.0f;

    clearTitleGlyphStrip_.animationTimer = 10.0f;
    playerTimeGlyphStrip_.animationTimer = 10.0f;
    ResetGlyphStrip(bestTimeGlyphStrip_);

    if (clearTimeUI_) {
        clearTimeUI_->SetTime(clearTimeValue_);
        clearTimeUI_->SetAlpha(1.0f);
    }
    if (bestTimeUI_) {
        bestTimeUI_->StartCountUp(bestTimeValue_, 0.85f);
    }
    if (diffTimeUI_) {
        diffTimeUI_->SetTime(std::fabs(diffTimeValue_));
        diffTimeUI_->SetAlpha(0.0f);
    }
    if (retryTextSprite_) retryTextSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    if (titleTextSprite_) titleTextSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
}

void GameClearScene::DrawImGui() {
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("Game Clear UI Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("ベスト更新演出を再生", ImVec2(-1.0f, 28.0f))) {
            PreviewNewBestEffect();
        }
        ImGui::Text("New Best Alpha: %.2f", newBestAlpha_);
        ImGui::Text("Best Time Alpha: %.2f", bestTimeAlpha_);
    }
#endif
}

void GameClearScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/white.png");

    objectManager_ = std::make_unique<ObjectManager>();

    // --- セーブデータ読み込み ---
    SaveDataManager::GetInstance()->Load();
    clearTimeValue_ = SaveDataManager::GetInstance()->GetLatestClearTime();
    bestTimeValue_ = SaveDataManager::GetInstance()->GetBestTime();
    float comparisonBestTime = SaveDataManager::GetInstance()->GetPreviousBestTime();
    isNewBest_ = SaveDataManager::GetInstance()->WasLatestClearBest();
    if (comparisonBestTime <= 0.0f || comparisonBestTime >= 9999.0f || !isNewBest_) {
        comparisonBestTime = bestTimeValue_;
    }
    diffTimeValue_ = clearTimeValue_ - comparisonBestTime;
    diffIsPositive_ = diffTimeValue_ > 0.005f;

    // --- タイムUI初期化（右寄せ配置） ---
    clearTimeUI_ = std::make_unique<TimeAttackUI>();
    clearTimeUI_->Initialize(spriteCommon_.get());
    clearTimeUI_->SetPosition({ 950.0f, 350.0f }, kResultMainTimeSpacing);
    clearTimeUI_->SetScale(kResultMainTimeScale);
    clearTimeUI_->SetTime(clearTimeValue_);
    clearTimeUI_->SetAlpha(0.0f);
    bestTimeUI_ = std::make_unique<TimeAttackUI>();
    bestTimeUI_->Initialize(spriteCommon_.get());
    bestTimeUI_->SetPosition({ 950.0f, 610.0f }, kResultMainTimeSpacing);
    bestTimeUI_->SetScale(kResultMainTimeScale);
    bestTimeUI_->SetTime(bestTimeValue_);
    bestTimeUI_->SetAlpha(0.0f);
    diffTimeUI_ = std::make_unique<TimeAttackUI>();
    diffTimeUI_->Initialize(spriteCommon_.get());
    diffTimeUI_->SetPosition({ 1332.0f, 610.0f }, 0.48f);
    diffTimeUI_->SetScale(kResultDiffTimeScale);
    diffTimeUI_->SetTime(std::fabs(diffTimeValue_));
    diffTimeUI_->SetAlpha(0.0f);
    InitializeResultUiSprites();
    // --- レベルデータ読み込み ---
    levelLoader_ = std::make_unique<LevelLoader>();
    levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/gameClearScene.json");
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/gameClearScene.json");

    // =========================================================
    // エディター配置スプライトの特定と初期非表示化
    // =========================================================
    for (auto& sprite : sprites_) {
 
        if (sprite->GetName() == "GameClear.png") gameClearSprite_ = sprite.get();
        if (sprite->GetName() == "restartText.png")     retryTextSprite_ = sprite.get();
        if (sprite->GetName() == "title.png")     titleTextSprite_ = sprite.get();
        if (sprite->GetName() == "playerTime.png") playerTimeSprite_ = sprite.get();
        if (sprite->GetName() == "bestTime.png") bestTimeSprite_ = sprite.get();
        if (sprite->GetName() == "enter_text.png") enterTextSprite_ = sprite.get();
    }
    if (gameClearSprite_) {
        gameClearSprite_->SetPosition({ 1120.0f, 104.0f });
        gameClearSprite_->SetSize({ 620.0f, 104.0f });
        gameClearSprite_->SetVisible(false);
    }
    if (playerTimeSprite_) {
        playerTimeSprite_->SetPosition({ 1110.0f, 238.0f });
        playerTimeSprite_->SetSize({ 450.0f, 90.0f });
        playerTimeSprite_->SetVisible(false);
    }
    if (bestTimeSprite_) {
        bestTimeSprite_->SetPosition({ 1110.0f, 498.0f });
        bestTimeSprite_->SetSize({ 450.0f, 90.0f });
        bestTimeSprite_->SetVisible(false);
    }
    InitializeClearTitleGlyphStrip();
    InitializePlayerTimeGlyphStrip();
    InitializeBestTimeGlyphStrip();
    if (enterTextSprite_) {
        enterTextBaseSize_ = { 275.0f, 46.0f };
        enterTextBasePosition_ = { 1378.0f, 805.0f };
        enterTextSprite_->SetPosition(enterTextBasePosition_);
        enterTextSprite_->SetSize(enterTextBaseSize_);
    }
    ApplyInputUiIfNeeded();

    auto HideSprite = [](Sprite* s) {
        if (s) { Vector4 c = s->GetColor(); c.w = 0.0f; s->SetColor(c); }
        };
    HideSprite(gameClearSprite_);
    HideSprite(retryTextSprite_);
    HideSprite(titleTextSprite_);
    HideSprite(playerTimeSprite_);
    HideSprite(bestTimeSprite_);
    HideSprite(enterTextSprite_);

    if (player_) {
        player_->SetIsControlActive(false);

        // 1. JSONで配置した位置（＝ガッツポーズを見せたい最高の場所）を記憶
        targetPlayerPos_ = player_->GetTransform()->translate;
        targetPlayerRot_ = player_->GetRotation();
        // 2. プレイヤーを画面の奥（または手前）にワープさせる
        Vector3 startPos = targetPlayerPos_;
        startPos.z += 15.0f;

        player_->GetTransform()->translate = startPos;
        player_->UpdateWorldMatrix();

        // 3. 最初は「走りステート」にする
        player_->ChangeState(std::make_unique<PlayerStateRun>());
    }
    LightManager::GetInstance()->LoadState("Resources/json/light/gameClearScene.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("gameClear_camera.json");
    gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
    GPUParticleManager::GetInstance()->PrewarmPreset(kClearVictoryStarBurstPreset);
    GPUParticleManager::GetInstance()->PrewarmPreset(kClearVictorySparklePreset);
    GPUParticleManager::GetInstance()->PrewarmPreset(kClearVictoryFloorTwinklePreset);
    PostEffect::GetInstance()->ResetToBaseParams();
    clearState_ = ClearState::kRunIn;
    stateTimer_ = 0.0f;
    resultAlpha_ = 0.0f;
    clearTimeAlpha_ = 0.0f;
    bestTimeAlpha_ = 0.0f; 
    menuAlpha_ = 0.0f;
    resultPanelAlpha_ = 0.0f;
    diffAlpha_ = 0.0f;
    inputGuideAlpha_ = 0.0f;
    newBestAlpha_ = 0.0f;
    clearTimePopTimer_ = -1.0f;
    bestTimePopTimer_ = -1.0f;
    ResetGlyphStrip(clearTitleGlyphStrip_);
    ResetGlyphStrip(playerTimeGlyphStrip_);
    ResetGlyphStrip(bestTimeGlyphStrip_);
    ResetVictoryPoseParticles();
}

void GameClearScene::Finalize() {
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();

    // スマートポインタの解放
    objectManager_.reset();
    sprites_.clear();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void GameClearScene::Update(float deltaTime) {
    ApplyInputUiIfNeeded();

    // ----------------------------------------------------------------
    // 1. 基本的なシステム更新 (常に動かすもの)
    // ----------------------------------------------------------------
    LightEditor::GetInstance()->Update();

    Object3d* cameraTarget = player_;
    if (!cameraTarget && objectManager_ && !objectManager_->GetObjects().empty()) {
        cameraTarget = objectManager_->GetObjects().front().get();
    }

    CameraEditor::GetInstance()->Update(cameraTarget, false);
    CameraManager::GetInstance()->Update();

    if (objectManager_) objectManager_->Update(deltaTime);
    particleSystem_->Update(deltaTime);
    GPUParticleManager::GetInstance()->Update(deltaTime);
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();

    // ----------------------------------------------------------------
    // 2. クリア演出シーケンス制御 (ステートマシン)
    // ----------------------------------------------------------------
    stateTimer_ += deltaTime;
    UpdateVictoryPoseParticles(deltaTime);

    switch (clearState_) {
    case ClearState::kRunIn:
        // --- 【フェーズ1】 入場：指定位置まで走る ---
        if (player_) {
            Vector3 currentPos = player_->GetTransform()->translate;
            Vector3 dir = { targetPlayerPos_.x - currentPos.x, 0.0f, targetPlayerPos_.z - currentPos.z };
            float dist = std::sqrt(dir.x * dir.x + dir.z * dir.z);

            if (dist > 0.5f) {
                dir.x /= dist; dir.z /= dist;
                float runSpeed = 12.0f;
                player_->SetVelocity({ dir.x * runSpeed, 0.0f, dir.z * runSpeed });
                float angle = std::atan2(dir.x, dir.z);
                player_->SetRotation({ 0.0f, angle, 0.0f });
            }
            else {
                player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
                player_->GetTransform()->translate = targetPlayerPos_;
                player_->SetRotation(targetPlayerRot_);
                player_->ChangeState(std::make_unique<PlayerStateWin>());
                ResetVictoryPoseParticles();
                ResetGlyphStrip(clearTitleGlyphStrip_);

                clearState_ = ClearState::kVictoryMotion;
                stateTimer_ = 0.0f;
            }
        }
        break;

    case ClearState::kVictoryMotion:
        // --- 【フェーズ2】 勝利：ジャンプの頂点でフリーズまで待機 ---
        resultPanelAlpha_ = Clamp01(resultPanelAlpha_ + deltaTime * 1.8f);
        resultAlpha_ = Clamp01(resultAlpha_ + deltaTime * 2.4f);
        if (stateTimer_ > 0.6f) {
            clearState_ = ClearState::kShowClearTime;
            stateTimer_ = 0.0f;

            // カメラ演出aを再生
            Camera* mainCamera = CameraManager::GetInstance()->GetMainCamera();
            if (mainCamera) {
                CameraEditor::GetInstance()->PlayOverrideCamera(mainCamera, "a");
            }

            clearTimeAlpha_ = 0.0f;
            clearTimePopTimer_ = -1.0f;
            ResetGlyphStrip(playerTimeGlyphStrip_);
            if (clearTimeUI_) clearTimeUI_->StartCountUp(clearTimeValue_, 1.15f);
        }
        break;

    case ClearState::kShowClearTime:
        // --- 【フェーズ3-A】 自己タイム表示（ドラムロール） ---
        resultPanelAlpha_ = Clamp01(resultPanelAlpha_ + deltaTime * 1.8f);
        resultAlpha_ = Clamp01(resultAlpha_ + deltaTime * 2.0f);
        clearTimeAlpha_ = Clamp01(clearTimeAlpha_ + deltaTime * 2.4f);

        if (clearTimeUI_ && !clearTimeUI_->IsAnimating()) {
            if (clearTimePopTimer_ < 0.0f) {
                clearTimePopTimer_ = 0.0f;
            } else {
                clearTimePopTimer_ += deltaTime;
            }
        }

        if (clearTimeUI_ && !clearTimeUI_->IsAnimating() && clearTimePopTimer_ > 0.48f) {
            clearState_ = ClearState::kShowBestTime;
            stateTimer_ = 0.0f;
            bestTimeAlpha_ = 0.0f;
            bestTimePopTimer_ = -1.0f;
            ResetGlyphStrip(bestTimeGlyphStrip_);
            if (bestTimeUI_) bestTimeUI_->StartCountUp(bestTimeValue_, 0.85f);
        }
        break;

    case ClearState::kShowBestTime:
        // --- 【フェーズ3-B】 ベストタイム表示（ドラムロール） ---
        bestTimeAlpha_ = Clamp01(bestTimeAlpha_ + deltaTime * 2.4f);

        if (bestTimeUI_ && !bestTimeUI_->IsAnimating()) {
            if (bestTimePopTimer_ < 0.0f) {
                bestTimePopTimer_ = 0.0f;
            } else {
                bestTimePopTimer_ += deltaTime;
            }
            diffAlpha_ = Clamp01(diffAlpha_ + deltaTime * 2.8f);
            if (isNewBest_) {
                newBestAlpha_ = Clamp01(newBestAlpha_ + deltaTime * 2.4f);
            }
        }

        if (bestTimeUI_ && !bestTimeUI_->IsAnimating() && bestTimePopTimer_ > 0.75f) {
            clearState_ = ClearState::kWaitInput;
            stateTimer_ = 0.0f;
        }
        break;

    case ClearState::kWaitInput:
        // --- 【フェーズ3-C】 入力待ち：アクション「Jump」(Space/A)で次へ ---
        inputGuideAlpha_ = Clamp01(inputGuideAlpha_ + deltaTime * 1.8f);
        if (inputManager_->IsActionTriggered("Jump")) {
            if (clearTimeUI_) clearTimeUI_->SetAlpha(0.0f);
            if (bestTimeUI_) bestTimeUI_->SetAlpha(0.0f);
            if (diffTimeUI_) diffTimeUI_->SetAlpha(0.0f);
            if (gameClearSprite_) gameClearSprite_->SetColor({ 1,1,1,0.0f });
            if (playerTimeSprite_) playerTimeSprite_->SetColor({ 1,1,1,0.0f });
     
            if (bestTimeSprite_) bestTimeSprite_->SetColor({ 1,1,1,0.0f });
            resultAlpha_ = 0.0f;
            clearTimeAlpha_ = 0.0f;
            bestTimeAlpha_ = 0.0f;
            resultPanelAlpha_ = 0.0f;
            diffAlpha_ = 0.0f;
            inputGuideAlpha_ = 0.0f;
            newBestAlpha_ = 0.0f;
            ResetGlyphStrip(clearTitleGlyphStrip_);

            // カメラ演出終了
            Camera* mainCamera = CameraManager::GetInstance()->GetMainCamera();
            if (mainCamera) {
                mainCamera->EndOverride(1.0f);
            }

            if (player_) {
                player_->ChangeState(std::make_unique<PlayerStateWinReturn>(targetPlayerPos_.y));
            }

            clearState_ = ClearState::kShowMenu;
            stateTimer_ = 0.0f;
        }
        break;

    case ClearState::kShowMenu:
        // --- 【フェーズ4】 メニュー：左右キー(A/D、左/右、スティック)で選択 ---
        menuAlpha_ += deltaTime * 2.0f;
        if (menuAlpha_ > 1.0f) menuAlpha_ = 1.0f;
        inputGuideAlpha_ = Clamp01(inputGuideAlpha_ + deltaTime * 1.8f);

        // アクション名「Left」「Right」で判定
        if (inputManager_->IsActionTriggered("Left")) {
            currentMenuIndex_ = (int)MenuIndex::Retry;
        }
        if (inputManager_->IsActionTriggered("Right")) {
            currentMenuIndex_ = (int)MenuIndex::Title;
        }

        // 強調演出の更新
        {
            const float blink = 0.5f + 0.5f * std::sin(stateTimer_ * 6.0f);
            auto ApplyEffect = [&](Sprite* s, bool isSelected, const Vector2& baseSize) {
                if (!s) return;
                const float selectedAlpha = 0.62f + 0.38f * blink;
                float alpha = (isSelected ? selectedAlpha : 0.3f) * menuAlpha_;
                Vector4 color = isSelected ? Vector4{ 1, 1, 1, alpha } : Vector4{ 0.5f, 0.5f, 0.5f, alpha };
                s->SetColor(color);
                const float selectedScale = 1.07f + 0.05f * blink;
                s->SetSize(isSelected ? Vector2{ baseSize.x * selectedScale, baseSize.y * selectedScale } : baseSize);
                };
            ApplyEffect(retryTextSprite_, currentMenuIndex_ == (int)MenuIndex::Retry, { 320.0f, 80.0f });
            ApplyEffect(titleTextSprite_, currentMenuIndex_ == (int)MenuIndex::Title, { 352.0f, 88.0f });
        }

        // 決定：アクション「Jump」(Space/A)で選んだ方へ走り出す
        if (inputManager_->IsActionTriggered("Jump")) {
            if (player_) {
                float runSpeed = 15.0f;
                float pi = 3.14159265f;
                if (currentMenuIndex_ == (int)MenuIndex::Retry) {
                    player_->SetVelocity({ -runSpeed, 0.0f, 0.0f });
                    player_->SetRotation({ 0.0f, -pi / 2.0f, 0.0f });
                }
                else {
                    player_->SetVelocity({ runSpeed, 0.0f, 0.0f });
                    player_->SetRotation({ 0.0f, pi / 2.0f, 0.0f });
                }
                player_->ChangeState(std::make_unique<PlayerStateRun>());
            }
            clearState_ = ClearState::kRunOut;
            stateTimer_ = 0.0f;
        }
        break;

    case ClearState::kRunOut:
        // --- 【フェーズ5】 退場：画面外へダッシュ ---
        resultAlpha_ = Clamp01(resultAlpha_ - deltaTime * 3.0f);
        clearTimeAlpha_ = Clamp01(clearTimeAlpha_ - deltaTime * 3.0f);
        bestTimeAlpha_ = Clamp01(bestTimeAlpha_ - deltaTime * 3.0f);
        resultPanelAlpha_ = Clamp01(resultPanelAlpha_ - deltaTime * 3.0f);
        diffAlpha_ = Clamp01(diffAlpha_ - deltaTime * 3.0f);
        inputGuideAlpha_ = Clamp01(inputGuideAlpha_ - deltaTime * 3.0f);
        newBestAlpha_ = Clamp01(newBestAlpha_ - deltaTime * 3.0f);
        menuAlpha_ = Clamp01(menuAlpha_ - deltaTime * 3.0f);

        if (retryTextSprite_) retryTextSprite_->SetColor({ 1,1,1, (currentMenuIndex_ == 0 ? 1.0f : 0.3f) * menuAlpha_ });
        if (titleTextSprite_) titleTextSprite_->SetColor({ 1,1,1, (currentMenuIndex_ == 1 ? 1.0f : 0.3f) * menuAlpha_ });

        if (stateTimer_ > 1.5f) {
            SceneManager::GetInstance()->ChangeScene(currentMenuIndex_ == 0 ? "GAMEPLAY" : "TITLE");
        }
        break;
    }
    UpdateResultUiVisuals(deltaTime);

    // ----------------------------------------------------------------
    // 3. 行列更新
    // ----------------------------------------------------------------
    if (clearTimeUI_) clearTimeUI_->Update(deltaTime);
    if (bestTimeUI_) bestTimeUI_->Update(deltaTime);
    if (diffTimeUI_) diffTimeUI_->Update(deltaTime);
    for (auto& sprite : sprites_) sprite->Update();
}
void GameClearScene::Draw() {
    // --- 一人称視点判定 ---
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
#ifndef _DEBUG
    if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    // --- 1. 不透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7) continue; // フォグ(7)も不透明パスから除外
        obj->Draw(pointLightRes, spotLightRes);
    }

    // --- 2. 中間描画 (弾・デバッグ) ---
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    LightEditor::GetInstance()->Draw3D();

    // --- 3. 透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1) { // 透明のみ描画
            obj->Draw(pointLightRes, spotLightRes);
        }
    }
    particleSystem_->Draw();

    // =======================================================
    // 4. ローカルフォグ (霧の箱) の描画
    // =======================================================
    bool hasFog = false;
    for (auto& obj : objects) {
        if (obj->GetMaterialType() == 7) hasFog = true;
    }

    if (hasFog) {
        dxCommon_->PreDrawLocalFog();
        for (auto& obj : objects) {
            if (obj->GetMaterialType() == 7) {
                obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
            }
        }
        dxCommon_->PostDrawLocalFog();
    }

    // =======================================================
    // 5. GPUパーティクルの描画
    // =======================================================
    dxCommon_->UpdateGrabTexture();
    dxCommon_->PreDrawLocalFog();
    if (camera) {
        GPUParticleManager::GetInstance()->Draw(
            dxCommon_->GetCommandList(),
            camera->GetViewMatrix(),
            camera->GetProjectionMatrix(),
            gpuParticleTexHandle_,
            dxCommon_->GetDepthSrvHandle()
        );
    }
    dxCommon_->PostDrawLocalFog();
}



// ====================================================================
// UI描画専用の関数
// ====================================================================
void GameClearScene::DrawUI() {
    // --- 4. 2D描画 (UIスプライト) ---
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    auto DrawSprite = [](const std::unique_ptr<Sprite>& sprite) {
        if (sprite) {
            sprite->Draw();
        }
    };
    DrawSprite(resultPanelSprite_);
    DrawSprite(resultPanelTopLineSprite_);
    DrawSprite(resultPanelBottomLineSprite_);
    DrawSprite(bestHighlightSprite_);
    DrawSprite(bestHighlightTopLineSprite_);
    DrawSprite(bestHighlightBottomLineSprite_);

    for (auto& sprite : sprites_) {
        sprite->Draw();
    }
    DrawGlyphStrip(clearTitleGlyphStrip_);
    DrawGlyphStrip(playerTimeGlyphStrip_);
    DrawGlyphStrip(bestTimeGlyphStrip_);
    DrawSprite(diffSignHorizontalSprite_);
    DrawSprite(diffSignVerticalSprite_);
    if (clearTimeUI_) clearTimeUI_->Draw();
    if (bestTimeUI_) bestTimeUI_->Draw();
    if (diffTimeUI_) diffTimeUI_->Draw();
}

// シャドウマップ描画の実装
void GameClearScene::DrawShadow() {
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

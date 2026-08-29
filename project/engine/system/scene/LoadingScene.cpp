#include "LoadingScene.h"

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kGaugeWidth = 680.0f;
constexpr float kGaugeHeight = 26.0f;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float ScreenW() {
    return static_cast<float>(WinApp::kClientWidth);
}

float ScreenH() {
    return static_cast<float>(WinApp::kClientHeight);
}

float GaugeLeft() {
    return (ScreenW() - kGaugeWidth) * 0.5f;
}

float GaugeY() {
    return ScreenH() - 72.0f;
}
}

void LoadingScene::Initialize() {
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(DirectXCommon::GetInstance());

    const uint32_t whiteHandle =
        TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    const uint32_t circleHandle =
        TextureManager::GetInstance()->Load("Resources/sprite/common/circle2.png");

    background_ = CreateSprite(
        whiteHandle,
        { ScreenW() * 0.5f, ScreenH() * 0.5f },
        { ScreenW(), ScreenH() },
        { 0.015f, 0.025f, 0.045f, 1.0f });

    dots_.clear();
    for (int i = 0; i < 3; ++i) {
        dots_.push_back(CreateSprite(
            circleHandle,
            { ScreenW() * 0.5f - 24.0f + static_cast<float>(i) * 24.0f, ScreenH() - 128.0f },
            { 12.0f, 12.0f },
            { 0.64f, 0.92f, 1.0f, 1.0f }));
    }

    const float gaugeCenterX = ScreenW() * 0.5f;
    gaugeShadow_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX + 3.0f, GaugeY() + 4.0f },
        { kGaugeWidth + 10.0f, 36.0f },
        { 0.0f, 0.0f, 0.0f, 0.35f });
    gaugeFrame_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX, GaugeY() },
        { kGaugeWidth + 6.0f, 32.0f },
        { 0.06f, 0.14f, 0.22f, 1.0f });
    gaugeTrack_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX, GaugeY() },
        { kGaugeWidth, kGaugeHeight },
        { 0.03f, 0.22f, 0.30f, 1.0f });

    gaugeFill_ = CreateSprite(
        whiteHandle,
        { GaugeLeft(), GaugeY() },
        { 0.0f, 18.0f },
        { 0.20f, 0.82f, 1.0f, 1.0f });
    gaugeFill_->SetAnchorPoint({ 0.0f, 0.5f });

    gaugeHighlight_ = CreateSprite(
        whiteHandle,
        { GaugeLeft(), GaugeY() - 5.0f },
        { 0.0f, 4.0f },
        { 0.82f, 1.0f, 1.0f, 0.72f });
    gaugeHighlight_->SetAnchorPoint({ 0.0f, 0.5f });

    gaugeGlow_ = CreateSprite(
        circleHandle,
        { GaugeLeft(), GaugeY() },
        { 44.0f, 44.0f },
        { 0.40f, 0.96f, 1.0f, 0.0f });

    gaugeBubbles_.clear();
    for (int i = 0; i < 12; ++i) {
        gaugeBubbles_.push_back(CreateSprite(
            circleHandle,
            { GaugeLeft(), GaugeY() },
            { 18.0f, 18.0f },
            { 0.88f, 1.0f, 1.0f, 0.0f }));
    }

    markerShadow_ = CreateSprite(
        circleHandle,
        { GaugeLeft(), GaugeY() - 18.0f },
        { 52.0f, 16.0f },
        { 0.0f, 0.0f, 0.0f, 0.25f });
    marker_ = CreateSprite(
        circleHandle,
        { GaugeLeft(), GaugeY() - 48.0f },
        { 42.0f, 42.0f },
        { 0.34f, 0.88f, 1.0f, 1.0f });

    timer_ = 0.0f;
    targetProgress_ = 0.0f;
    displayedProgress_ = 0.0f;
    UpdateProgressGauge();
}

void LoadingScene::Update(float deltaTime) {
    const float effectiveDeltaTime = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);
    timer_ += effectiveDeltaTime;

    const float progressFollow = 1.0f - std::pow(0.001f, effectiveDeltaTime);
    displayedProgress_ += (targetProgress_ - displayedProgress_) * progressFollow;
    if (targetProgress_ >= 0.999f) {
        displayedProgress_ = (std::max)(displayedProgress_, 0.995f);
    }

    UpdateProgressGauge();
    UpdateMarkerAnimation();
    UpdateDotAnimation();

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Update();
        }
    }
}

void LoadingScene::Draw() {
}

void LoadingScene::DrawUI() {
    if (!spriteCommon_) {
        return;
    }

    spriteCommon_->SetPipeline(DirectXCommon::GetInstance()->GetCommandList());
    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Draw();
        }
    }
}

void LoadingScene::Finalize() {
    dots_.clear();
    gaugeBubbles_.clear();
    background_ = nullptr;
    marker_ = nullptr;
    markerShadow_ = nullptr;
    gaugeShadow_ = nullptr;
    gaugeFrame_ = nullptr;
    gaugeTrack_ = nullptr;
    gaugeFill_ = nullptr;
    gaugeHighlight_ = nullptr;
    gaugeGlow_ = nullptr;
    sprites_.clear();
    spriteCommon_.reset();
}

void LoadingScene::SetProgress(float progress) {
    targetProgress_ = Clamp01(progress);
}

Sprite* LoadingScene::CreateSprite(
    uint32_t textureHandle,
    const Vector2& position,
    const Vector2& size,
    const Vector4& color) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteCommon_.get(), textureHandle);
    sprite->SetAnchorPoint({ 0.5f, 0.5f });
    sprite->SetPosition(position);
    sprite->SetSize(size);
    sprite->SetColor(color);
    sprite->Update();

    Sprite* raw = sprite.get();
    sprites_.push_back(std::move(sprite));
    return raw;
}

void LoadingScene::UpdateMarkerAnimation() {
    if (!marker_ || !markerShadow_) {
        return;
    }

    const float progress = Clamp01(displayedProgress_);
    const float x = GaugeLeft() + kGaugeWidth * progress;
    const float hopPhase = std::fmod(timer_ * 1.6f, 1.0f);
    const float hop = (std::max)(0.0f, std::sin(hopPhase * kPi)) * 16.0f;
    const float pulse = 0.5f + 0.5f * std::sin(timer_ * 5.0f);

    marker_->SetPosition({ x, GaugeY() - 48.0f - hop });
    marker_->SetSize({ 40.0f + pulse * 5.0f, 40.0f + pulse * 5.0f });
    marker_->SetColor({ 0.24f + pulse * 0.12f, 0.78f + pulse * 0.16f, 1.0f, 1.0f });

    const float shadowScale = 1.0f - hop / 48.0f;
    markerShadow_->SetPosition({ x, GaugeY() - 17.0f });
    markerShadow_->SetSize({ 52.0f * shadowScale, 16.0f * shadowScale });
}

void LoadingScene::UpdateDotAnimation() {
    for (size_t i = 0; i < dots_.size(); ++i) {
        Sprite* dot = dots_[i];
        if (!dot) {
            continue;
        }

        const float phase = timer_ * 4.0f - static_cast<float>(i) * 0.58f;
        const float pulse = 0.5f + 0.5f * std::sin(phase);
        const float scale = 0.72f + pulse * 0.35f;
        dot->SetSize({ 12.0f * scale, 12.0f * scale });
        dot->SetColor({ 0.64f, 0.92f, 1.0f, 0.26f + pulse * 0.74f });
    }
}

void LoadingScene::UpdateProgressGauge() {
    if (!gaugeFill_ || !gaugeHighlight_ || !gaugeGlow_) {
        return;
    }

    const float progress = Clamp01(displayedProgress_);
    const float fillWidth = kGaugeWidth * progress;
    const float wave = std::sin(timer_ * 5.4f) * 1.2f;
    const float pulse = 0.5f + 0.5f * std::sin(timer_ * 4.2f);

    gaugeFill_->SetPosition({ GaugeLeft(), GaugeY() + wave * 0.20f });
    gaugeFill_->SetSize({ fillWidth, 18.0f + wave });
    gaugeFill_->SetColor({ 0.10f + pulse * 0.06f, 0.78f + pulse * 0.12f, 1.0f, 1.0f });

    gaugeHighlight_->SetPosition({ GaugeLeft(), GaugeY() - 5.0f + wave * 0.45f });
    gaugeHighlight_->SetSize({ fillWidth, 3.0f + pulse * 1.5f });
    gaugeHighlight_->SetColor({ 0.84f, 1.0f, 1.0f, 0.44f + pulse * 0.30f });

    gaugeGlow_->SetVisible(progress > 0.002f);
    gaugeGlow_->SetPosition({ GaugeLeft() + fillWidth, GaugeY() });
    gaugeGlow_->SetSize({ 36.0f + pulse * 8.0f, 36.0f + pulse * 8.0f });
    gaugeGlow_->SetColor({ 0.42f, 0.98f, 1.0f, 0.18f + pulse * 0.18f });

    for (size_t i = 0; i < gaugeBubbles_.size(); ++i) {
        Sprite* bubble = gaugeBubbles_[i];
        if (!bubble) {
            continue;
        }

        const float normalizedX =
            (static_cast<float>(i) + 0.5f) / static_cast<float>(gaugeBubbles_.size());
        const float bubbleX = GaugeLeft() + normalizedX * kGaugeWidth;
        const bool visible = fillWidth > normalizedX * kGaugeWidth + 6.0f;
        bubble->SetVisible(visible);
        if (!visible) {
            continue;
        }

        const float phase = timer_ * 3.0f - static_cast<float>(i) * 0.52f;
        const float rise = std::sin(phase);
        const float size =
            12.0f + (0.5f + 0.5f * std::sin(phase * 0.73f)) * 7.0f;
        bubble->SetPosition({ bubbleX, GaugeY() + rise * 3.0f });
        bubble->SetSize({ size, size });
        bubble->SetColor({ 0.74f, 1.0f, 1.0f, 0.16f + pulse * 0.20f });
    }
}

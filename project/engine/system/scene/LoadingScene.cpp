#include "LoadingScene.h"

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "WinApp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kGaugeWidth = 680.0f;
constexpr float kGaugeHeight = 26.0f;
constexpr float kSlimeFrameWidth = 543.0f;
constexpr float kSlimeFrameHeight = 724.0f;

const char* SelectLoadingSlimeTexturePath() {
    // 通常スライムを主役にしつつ、低確率で属性違いが登場するようにする。
    static constexpr std::array<const char*, 4> kTexturePaths = {
        "Resources/sprite/loading/slime_hop_sheet_right.png",
        "Resources/sprite/loading/slime_hop_sheet_right_thunder.png",
        "Resources/sprite/loading/slime_hop_sheet_right_fire.png",
        "Resources/sprite/loading/slime_hop_sheet_right_bomber.png",
    };
    static constexpr std::array<int, 4> kAppearanceWeights = { 70, 10, 10, 10 };
    static std::mt19937 randomEngine(std::random_device{}());

    std::discrete_distribution<int> distribution(kAppearanceWeights.begin(), kAppearanceWeights.end());
    return kTexturePaths[static_cast<std::size_t>(distribution(randomEngine))];
}

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

    const uint32_t whiteHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    const uint32_t glowHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/circle2.png");
    const uint32_t fadeFinalHandle = TextureManager::GetInstance()->Load("Resources/sprite/fade/crown_iris/frame_47.png");
    const uint32_t slimeHandle = TextureManager::GetInstance()->Load(SelectLoadingSlimeTexturePath());
    const uint32_t loadingTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/generated/text/text_text_load.png");
    const uint32_t hintTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/generated/text/text_loading_hint.png");
    const uint32_t dotHandle = TextureManager::GetInstance()->Load("Resources/sprite/number/dot.png");

    background_ = CreateSprite(
        fadeFinalHandle,
        { ScreenW() * 0.5f, ScreenH() * 0.5f },
        { ScreenW(), ScreenH() },
        { 1.0f, 1.0f, 1.0f, 1.0f });

    hintText_ = CreateSprite(
        hintTextHandle,
        { ScreenW() * 0.5f, ScreenH() * 0.22f },
        { 180.0f, 72.0f },
        { 1.0f, 1.0f, 1.0f, 0.95f });

    loadingText_ = CreateSprite(
        loadingTextHandle,
        { ScreenW() * 0.5f - 18.0f, ScreenH() - 132.0f },
        { 154.0f, 56.0f },
        { 0.88f, 0.98f, 1.0f, 1.0f });

    dots_.clear();
    for (int i = 0; i < 3; ++i) {
        dots_.push_back(CreateSprite(
            dotHandle,
            { ScreenW() * 0.5f + 62.0f + static_cast<float>(i) * 18.0f, ScreenH() - 119.0f },
            { 10.0f, 10.0f },
            { 0.88f, 0.98f, 1.0f, 1.0f }));
    }

    const float gaugeCenterX = ScreenW() * 0.5f;

    gaugeShadow_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX + 3.0f, GaugeY() + 4.0f },
        { kGaugeWidth + 10.0f, 36.0f },
        { 0.0f, 0.08f, 0.12f, 0.30f });

    gaugeFrame_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX, GaugeY() },
        { kGaugeWidth + 6.0f, 32.0f },
        { 0.015f, 0.12f, 0.20f, 0.98f });

    gaugeTrack_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX, GaugeY() },
        { kGaugeWidth, kGaugeHeight },
        { 0.02f, 0.25f, 0.32f, 0.96f });

    gaugeFill_ = CreateSprite(
        whiteHandle,
        { GaugeLeft(), GaugeY() },
        { 0.0f, 18.0f },
        { 0.20f, 0.92f, 1.0f, 1.0f });
    gaugeFill_->SetAnchorPoint({ 0.0f, 0.5f });

    gaugeHighlight_ = CreateSprite(
        whiteHandle,
        { GaugeLeft(), GaugeY() - 5.0f },
        { 0.0f, 5.0f },
        { 0.78f, 1.0f, 1.0f, 0.80f });
    gaugeHighlight_->SetAnchorPoint({ 0.0f, 0.5f });

    gaugeGlow_ = CreateSprite(
        glowHandle,
        { GaugeLeft(), GaugeY() },
        { 44.0f, 44.0f },
        { 0.40f, 0.96f, 1.0f, 0.0f });

    gaugeBubbles_.clear();
    for (int i = 0; i < 12; ++i) {
        gaugeBubbles_.push_back(CreateSprite(
            glowHandle,
            { GaugeLeft(), GaugeY() },
            { 18.0f, 18.0f },
            { 0.88f, 1.0f, 1.0f, 0.0f }));
    }

    shadow_ = CreateSprite(
        glowHandle,
        { GaugeLeft(), ScreenH() - 171.0f },
        { 96.0f, 30.0f },
        { 0.0f, 0.10f, 0.15f, 0.30f });

    slime_ = CreateSprite(
        slimeHandle,
        { GaugeLeft(), ScreenH() - 203.0f },
        { 116.0f, 130.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f });
    slime_->SetTextureRect({ 0.0f, 0.0f }, { kSlimeFrameWidth, kSlimeFrameHeight });

    timer_ = 0.0f;
    targetProgress_ = 0.0f;
    displayedProgress_ = 0.0f;
    slimeFrame_ = -1;
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
    UpdateSlimeRunAnimation();
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
    loadingText_ = nullptr;
    slime_ = nullptr;
    shadow_ = nullptr;
    hintText_ = nullptr;
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

Sprite* LoadingScene::CreateSprite(uint32_t textureHandle, const Vector2& position, const Vector2& size, const Vector4& color) {
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

void LoadingScene::UpdateSlimeRunAnimation() {
    if (!slime_ || !shadow_) {
        return;
    }

    const float progress = Clamp01(displayedProgress_);
    const float x = GaugeLeft() + kGaugeWidth * progress;
    const float hopT = std::fmod(timer_ * 1.45f, 1.0f);
    int frame = 0;
    float lift = 0.0f;
    Vector2 size = { 116.0f, 130.0f };
    float rotation = 0.0f;

    if (hopT < 0.22f) {
        frame = 0;
        size = { 120.0f, 122.0f };
        lift = -2.0f;
    } else if (hopT < 0.42f) {
        frame = 1;
        size = { 112.0f, 138.0f };
        lift = 17.0f;
        rotation = 0.025f;
    } else if (hopT < 0.76f) {
        frame = 2;
        size = { 116.0f, 130.0f };
        const float airT = (hopT - 0.42f) / 0.34f;
        lift = 22.0f + std::sin(airT * kPi) * 9.0f;
        rotation = -0.018f;
    } else {
        frame = 3;
        size = { 130.0f, 112.0f };
        const float landT = (hopT - 0.76f) / 0.24f;
        lift = -4.0f + std::sin(landT * kPi) * 4.0f;
    }

    if (frame != slimeFrame_) {
        slimeFrame_ = frame;
        slime_->SetTextureRect(
            { kSlimeFrameWidth * static_cast<float>(frame), 0.0f },
            { kSlimeFrameWidth, kSlimeFrameHeight });
    }

    slime_->SetPosition({ x, ScreenH() - 203.0f - lift });
    slime_->SetSize(size);
    slime_->SetRotation(rotation);

    const float shadowScale = 1.0f - Clamp01(lift / 34.0f) * 0.34f;
    shadow_->SetPosition({ x, ScreenH() - 164.0f });
    shadow_->SetSize({ 96.0f * shadowScale, 30.0f * shadowScale });
    shadow_->SetColor({ 0.0f, 0.08f, 0.14f, 0.12f + shadowScale * 0.16f });
}

void LoadingScene::UpdateDotAnimation() {
    for (size_t i = 0; i < dots_.size(); ++i) {
        Sprite* dot = dots_[i];
        if (!dot) {
            continue;
        }

        const float phase = timer_ * 4.0f - static_cast<float>(i) * 0.58f;
        const float pulse = 0.5f + 0.5f * std::sin(phase);
        const float alpha = 0.26f + pulse * 0.74f;
        const float scale = 0.72f + pulse * 0.35f;
        dot->SetSize({ 10.0f * scale, 10.0f * scale });
        dot->SetColor({ 0.88f, 0.98f, 1.0f, alpha });
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

        const float normalizedX = (static_cast<float>(i) + 0.5f) / static_cast<float>(gaugeBubbles_.size());
        const float bubbleX = GaugeLeft() + normalizedX * kGaugeWidth;
        const bool visible = fillWidth > normalizedX * kGaugeWidth + 6.0f;
        bubble->SetVisible(visible);
        if (!visible) {
            continue;
        }

        const float phase = timer_ * 3.0f - static_cast<float>(i) * 0.52f;
        const float rise = std::sin(phase);
        const float size = 12.0f + (0.5f + 0.5f * std::sin(phase * 0.73f)) * 7.0f;
        bubble->SetPosition({ bubbleX, GaugeY() + rise * 3.0f });
        bubble->SetSize({ size, size });
        bubble->SetColor({ 0.74f, 1.0f, 1.0f, 0.16f + pulse * 0.20f });
    }
}

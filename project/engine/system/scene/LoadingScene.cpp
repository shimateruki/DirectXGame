#include "LoadingScene.h"

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPi = 3.1415926535f;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float ScreenW() {
    return static_cast<float>(WinApp::kClientWidth);
}

float ScreenH() {
    return static_cast<float>(WinApp::kClientHeight);
}
}

void LoadingScene::Initialize() {
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(DirectXCommon::GetInstance());

    const uint32_t whiteHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    const uint32_t fadeFinalHandle = TextureManager::GetInstance()->Load("Resources/sprite/fade/crown_iris/frame_47.png");
    const uint32_t slimeHandle = TextureManager::GetInstance()->Load("Resources/sprite/title/slime_save_icon.png");
    const uint32_t loadingTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/generated/text/text_text_load.png");
    const uint32_t hintTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/generated/text/text_loading_hint.png");
    const uint32_t dotHandle = TextureManager::GetInstance()->Load("Resources/sprite/number/dot.png");
    const uint32_t slashHandle = TextureManager::GetInstance()->Load("Resources/sprite/number/slash.png");
    for (int i = 0; i < 10; ++i) {
        digitTextureHandles_[i] = TextureManager::GetInstance()->Load("Resources/sprite/number/big" + std::to_string(i) + ".png");
    }

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
        { 112.0f, ScreenH() - 72.0f },
        { 168.0f, 62.0f },
        { 0.88f, 0.98f, 1.0f, 1.0f });

    dots_.clear();
    for (int i = 0; i < 3; ++i) {
        dots_.push_back(CreateSprite(
            dotHandle,
            { 198.0f + static_cast<float>(i) * 22.0f, ScreenH() - 55.0f },
            { 13.0f, 13.0f },
            { 0.88f, 0.98f, 1.0f, 1.0f }));
    }

    shadow_ = CreateSprite(
        whiteHandle,
        { 300.0f, ScreenH() - 51.0f },
        { 92.0f, 16.0f },
        { 0.0f, 0.0f, 0.0f, 0.35f });

    slime_ = CreateSprite(
        slimeHandle,
        { 300.0f, ScreenH() - 102.0f },
        { 86.0f, 86.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f });

    progressDigits_.clear();
    for (int i = 0; i < 3; ++i) {
        Sprite* digit = CreateSprite(
            digitTextureHandles_[0],
            { ScreenW() - 152.0f + static_cast<float>(i) * 30.0f, ScreenH() - 55.0f },
            { 28.0f, 28.0f },
            { 0.90f, 1.0f, 1.0f, 1.0f });
        progressDigits_.push_back(digit);
    }
    progressSlash_ = CreateSprite(
        slashHandle,
        { ScreenW() - 72.0f, ScreenH() - 55.0f },
        { 21.0f, 31.0f },
        { 0.90f, 1.0f, 1.0f, 1.0f });
    progressDotTop_ = CreateSprite(
        dotHandle,
        { ScreenW() - 84.0f, ScreenH() - 65.0f },
        { 7.0f, 7.0f },
        { 0.90f, 1.0f, 1.0f, 1.0f });
    progressDotBottom_ = CreateSprite(
        dotHandle,
        { ScreenW() - 61.0f, ScreenH() - 45.0f },
        { 7.0f, 7.0f },
        { 0.90f, 1.0f, 1.0f, 1.0f });

    timer_ = 0.0f;
    targetProgress_ = 0.0f;
    displayedProgress_ = 0.0f;
    lastDisplayedPercent_ = -1;
    UpdateProgressSprites();
}

void LoadingScene::Update(float deltaTime) {
    const float effectiveDeltaTime = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);
    timer_ += effectiveDeltaTime;

    const float progressFollow = 1.0f - std::pow(0.001f, effectiveDeltaTime);
    displayedProgress_ += (targetProgress_ - displayedProgress_) * progressFollow;
    if (targetProgress_ >= 0.999f) {
        displayedProgress_ = (std::max)(displayedProgress_, 0.995f);
    }

    UpdateSlimeRunAnimation();
    UpdateDotAnimation();
    UpdateProgressSprites();

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
    progressDigits_.clear();
    background_ = nullptr;
    loadingText_ = nullptr;
    slime_ = nullptr;
    shadow_ = nullptr;
    hintText_ = nullptr;
    progressSlash_ = nullptr;
    progressDotTop_ = nullptr;
    progressDotBottom_ = nullptr;
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

    const float runStartX = 292.0f;
    const float runEndX = ScreenW() - 176.0f;
    const float runWidth = (std::max)(runEndX - runStartX, 1.0f);
    const float loopT = std::fmod(timer_ * 0.28f, 1.0f);
    const float x = runStartX + runWidth * loopT;

    const float jumpCycle = std::fmod(timer_ * 2.35f, 1.0f);
    const float jump = std::sin(jumpCycle * kPi);
    const float landT = jumpCycle > 0.78f ? Clamp01((jumpCycle - 0.78f) / 0.22f) : 0.0f;
    const float squash = std::sin(landT * kPi);
    const float baseY = ScreenH() - 102.0f;
    const float y = baseY - jump * 34.0f + squash * 8.0f;
    const float scaleX = 1.0f + squash * 0.18f;
    const float scaleY = 1.0f - squash * 0.16f + jump * 0.05f;
    const float sway = std::sin(timer_ * 7.0f) * 0.07f;

    slime_->SetPosition({ x, y });
    slime_->SetSize({ 86.0f * scaleX, 86.0f * scaleY });
    slime_->SetRotation(sway);

    const float shadowScale = 0.70f + (1.0f - jump) * 0.40f;
    shadow_->SetPosition({ x, ScreenH() - 51.0f });
    shadow_->SetSize({ 92.0f * shadowScale, 16.0f });
    shadow_->SetColor({ 0.0f, 0.0f, 0.0f, 0.18f + (1.0f - jump) * 0.18f });
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
        dot->SetSize({ 13.0f * scale, 13.0f * scale });
        dot->SetColor({ 0.88f, 0.98f, 1.0f, alpha });
    }
}

void LoadingScene::UpdateProgressSprites() {
    if (progressDigits_.empty() || !progressSlash_ || !progressDotTop_ || !progressDotBottom_) {
        return;
    }

    int percent = static_cast<int>(std::round(Clamp01(displayedProgress_) * 100.0f));
    percent = std::clamp(percent, 0, 100);
    if (percent == lastDisplayedPercent_) {
        return;
    }

    lastDisplayedPercent_ = percent;
    LayoutProgressSprites(percent);
}

void LoadingScene::LayoutProgressSprites(int percent) {
    const int clampedPercent = std::clamp(percent, 0, 100);
    int digits[3] = {
        clampedPercent / 100,
        (clampedPercent / 10) % 10,
        clampedPercent % 10
    };

    const int digitCount = clampedPercent >= 100 ? 3 : (clampedPercent >= 10 ? 2 : 1);
    const int firstDigitIndex = 3 - digitCount;
    const float digitWidth = 28.0f;
    const float digitGap = 3.0f;
    const float percentGap = 8.0f;
    const float percentWidth = 28.0f;
    const float totalWidth =
        static_cast<float>(digitCount) * digitWidth +
        static_cast<float>(digitCount - 1) * digitGap +
        percentGap +
        percentWidth;
    const float rightX = ScreenW() - 54.0f;
    const float baseY = ScreenH() - 55.0f;
    float x = rightX - totalWidth;

    for (size_t i = 0; i < progressDigits_.size(); ++i) {
        Sprite* digitSprite = progressDigits_[i];
        if (!digitSprite) {
            continue;
        }

        const int sourceIndex = firstDigitIndex + static_cast<int>(i);
        const bool visible = static_cast<int>(i) < digitCount;
        digitSprite->SetVisible(visible);
        if (!visible) {
            continue;
        }

        const int digit = digits[sourceIndex];
        digitSprite->SetTextureHandle(digitTextureHandles_[digit]);
        digitSprite->SetPosition({ x + digitWidth * 0.5f, baseY });
        digitSprite->SetSize({ digitWidth, digitWidth });
        digitSprite->SetColor({ 0.90f, 1.0f, 1.0f, 1.0f });
        x += digitWidth + digitGap;
    }

    x += percentGap;
    progressSlash_->SetPosition({ x + 14.0f, baseY });
    progressSlash_->SetSize({ 21.0f, 31.0f });
    progressSlash_->SetColor({ 0.90f, 1.0f, 1.0f, 1.0f });
    progressDotTop_->SetPosition({ x + 6.0f, baseY - 10.0f });
    progressDotBottom_->SetPosition({ x + 25.0f, baseY + 10.0f });
    progressDotTop_->SetSize({ 7.0f, 7.0f });
    progressDotBottom_->SetSize({ 7.0f, 7.0f });
    progressDotTop_->SetColor({ 0.90f, 1.0f, 1.0f, 1.0f });
    progressDotBottom_->SetColor({ 0.90f, 1.0f, 1.0f, 1.0f });
}

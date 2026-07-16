#include "Fade.h"

#include "DirectXCommon.h"
#include "PostEffect.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
float ScreenW() {
    return static_cast<float>(WinApp::kClientWidth);
}

float ScreenH() {
    return static_cast<float>(WinApp::kClientHeight);
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseInOut(float t) {
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

std::string MakeFramePath(const std::string& directory, int index) {
    char buffer[256] = {};
    std::snprintf(buffer, sizeof(buffer), "%s/frame_%02d.png", directory.c_str(), index);
    return buffer;
}
}

Fade* Fade::GetInstance() {
    static Fade instance;
    return &instance;
}

void Fade::Initialize() {
    const uint32_t noiseHandle = TextureManager::GetInstance()->Load(
        "Resources/sprite/effect/noise0.png",
        TextureManager::TextureColorSpace::Linear);
    PostEffect::GetInstance()->SetNoiseTexture(noiseHandle);
    ResetPostEffectFade();
    InitializeSprites();
}

void Fade::InitializeSprites() {
    if (spriteCommon_) {
        return;
    }

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(DirectXCommon::GetInstance());

    const uint32_t whiteHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    fallbackBlack_ = std::make_unique<Sprite>();
    fallbackBlack_->Initialize(spriteCommon_.get(), whiteHandle);
    fallbackBlack_->SetAnchorPoint({ 0.5f, 0.5f });

    LoadSequence(slimeWipe_, "Resources/sprite/fade/slime_wipe", 48);
    LoadSequence(crownIris_, "Resources/sprite/fade/crown_iris", 48);
}

void Fade::LoadSequence(FrameSequence& sequence, const std::string& directory, int frameCount) {
    if (!sequence.textureHandles.empty()) {
        return;
    }

    sequence.textureHandles.reserve(frameCount);
    for (int i = 0; i < frameCount; ++i) {
        sequence.textureHandles.push_back(TextureManager::GetInstance()->Load(MakeFramePath(directory, i)));
    }

    if (!sequence.textureHandles.empty()) {
        sequence.sprite = std::make_unique<Sprite>();
        sequence.sprite->Initialize(spriteCommon_.get(), sequence.textureHandles.front());
        sequence.sprite->SetAnchorPoint({ 0.5f, 0.5f });
    }
}

void Fade::Update(float deltaTime) {
    if (status_ == Status::None || status_ == Status::Finished) {
        return;
    }

    counter_ += deltaTime;
    const float t = GetNormalizedTime();
    coverage_ = IsClosing() ? EaseInOut(t) : 1.0f - EaseInOut(t);

    ResetPostEffectFade();

    if (t >= 1.0f) {
        coverage_ = IsClosing() ? 1.0f : 0.0f;
        status_ = Status::Finished;
    }
}

void Fade::Draw() {
    if (coverage_ <= 0.001f) {
        return;
    }

    InitializeSprites();

    if (style_ == VisualStyle::CrownIris) {
        DrawFrameSequence(crownIris_, coverage_);
    } else {
        DrawFrameSequence(slimeWipe_, coverage_);
    }
}

void Fade::StartFadeIn(float duration) {
    Begin(Status::FadeIn, VisualStyle::CrownIris, duration, { 0.5f, 0.5f }, 1.0f);
}

void Fade::StartFadeOut(float duration) {
    Begin(Status::FadeOut, VisualStyle::CrownIris, duration, { 0.5f, 0.5f }, 0.0f);
}

void Fade::StartIrisOut(float duration, const Vector2& center) {
    Begin(Status::IrisOut, VisualStyle::CrownIris, duration, center, 0.0f);
}

void Fade::StartIrisIn(float duration, const Vector2& center) {
    Begin(Status::IrisIn, VisualStyle::CrownIris, duration, center, 1.0f);
}

void Fade::Stop() {
    status_ = Status::None;
    counter_ = 0.0f;
    coverage_ = 0.0f;
    ResetPostEffectFade();
}

void Fade::Begin(Status status, VisualStyle style, float duration, const Vector2& center, float initialCoverage) {
    InitializeSprites();
    status_ = status;
    style_ = style;
    duration_ = (std::max)(duration, 0.001f);
    counter_ = 0.0f;
    center_ = { Clamp01(center.x), Clamp01(center.y) };
    coverage_ = Clamp01(initialCoverage);
    ResetPostEffectFade();
}

void Fade::DrawFrameSequence(FrameSequence& sequence, float coverage) {
    if (!sequence.sprite || sequence.textureHandles.empty()) {
        DrawFallbackBlack(coverage);
        return;
    }

    const float framePosition = Clamp01(coverage) * static_cast<float>(sequence.textureHandles.size() - 1);
    const size_t frameIndex = static_cast<size_t>(std::round(framePosition));
    const uint32_t textureHandle = sequence.textureHandles[(std::min)(frameIndex, sequence.textureHandles.size() - 1)];

    sequence.sprite->SetTextureHandle(textureHandle);
    sequence.sprite->SetPosition({ ScreenW() * 0.5f, ScreenH() * 0.5f });
    sequence.sprite->SetSize({ ScreenW(), ScreenH() });
    sequence.sprite->SetRotation(0.0f);
    sequence.sprite->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    sequence.sprite->Update();
    sequence.sprite->Draw();
}

void Fade::DrawFallbackBlack(float alpha) {
    if (!fallbackBlack_) {
        return;
    }

    fallbackBlack_->SetPosition({ ScreenW() * 0.5f, ScreenH() * 0.5f });
    fallbackBlack_->SetSize({ ScreenW() + 8.0f, ScreenH() + 8.0f });
    fallbackBlack_->SetRotation(0.0f);
    fallbackBlack_->SetColor({ 0.0f, 0.0f, 0.0f, Clamp01(alpha) });
    fallbackBlack_->Update();
    fallbackBlack_->Draw();
}

float Fade::GetNormalizedTime() const {
    return Clamp01(counter_ / (std::max)(duration_, 0.001f));
}

bool Fade::IsClosing() const {
    return status_ == Status::FadeOut || status_ == Status::IrisOut;
}

void Fade::ResetPostEffectFade() {
    if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
        params->slimeFadeIntensity = 0.0f;
        params->irisFadeIntensity = 0.0f;
    }
}

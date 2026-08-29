#include "Fade.h"

#include "DirectXCommon.h"
#include "PostEffect.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "WinApp.h"

#include <algorithm>

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

float EaseInOut(float value) {
    const float t = Clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}
}

Fade* Fade::GetInstance() {
    static Fade instance;
    return &instance;
}

void Fade::Initialize() {
    ResolveNoiseTexture();
    ResetPostEffectFade();
    InitializeSprites();
}

void Fade::InitializeSprites() {
    if (fallbackBlack_) {
        return;
    }

    if (!spriteCommon_) {
        spriteCommon_ = std::make_unique<SpriteCommon>();
        spriteCommon_->Initialize(DirectXCommon::GetInstance());
    }

    const uint32_t whiteHandle =
        TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    fallbackBlack_ = std::make_unique<Sprite>();
    fallbackBlack_->Initialize(spriteCommon_.get(), whiteHandle);
    fallbackBlack_->SetAnchorPoint({ 0.5f, 0.5f });
}

void Fade::ResolveNoiseTexture() {
    if (noiseTextureResolved_) {
        return;
    }

    const uint32_t noiseHandle =
        TextureManager::GetInstance()->Load("Resources/sprite/effect/noise0.png");
    PostEffect::GetInstance()->SetNoiseTexture(noiseHandle);
    noiseTextureResolved_ = true;
}

void Fade::Update(float deltaTime) {
    ResolveNoiseTexture();
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
    DrawFallbackBlack(coverage_);
}

void Fade::StartFadeIn(float duration) {
    Begin(Status::FadeIn, duration, { 0.5f, 0.5f }, 1.0f);
}

void Fade::StartFadeOut(float duration) {
    Begin(Status::FadeOut, duration, { 0.5f, 0.5f }, 0.0f);
}

void Fade::StartIrisOut(float duration, const Vector2& center) {
    Begin(Status::IrisOut, duration, center, 0.0f);
}

void Fade::StartIrisIn(float duration, const Vector2& center) {
    Begin(Status::IrisIn, duration, center, 1.0f);
}

void Fade::Stop() {
    status_ = Status::None;
    counter_ = 0.0f;
    coverage_ = 0.0f;
    ResetPostEffectFade();
}

void Fade::Begin(
    Status status,
    float duration,
    const Vector2& center,
    float initialCoverage) {
    InitializeSprites();
    status_ = status;
    duration_ = (std::max)(duration, 0.001f);
    counter_ = 0.0f;
    center_ = { Clamp01(center.x), Clamp01(center.y) };
    coverage_ = Clamp01(initialCoverage);
    ResetPostEffectFade();
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

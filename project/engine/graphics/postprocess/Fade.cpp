#include "Fade.h"
#include "PostEffect.h"
#include "engine/graphics/core/TextureManager.h"
#include <algorithm>

namespace {
PostEffect::Params* GetPostEffectParams() {
    return PostEffect::GetInstance()->GetParams();
}
}

Fade* Fade::GetInstance() {
    static Fade instance;
    return &instance;
}

void Fade::Initialize() {
    // スライムフェード用のノイズテクスチャをロードしてセット
    uint32_t noiseHandle = TextureManager::GetInstance()->Load("Resources/sprite/effect/noise0.png");
    PostEffect::GetInstance()->SetNoiseTexture(noiseHandle);
}

void Fade::Update(float deltaTime) {
    if (status_ == Status::None || status_ == Status::Finished) {
        return;
    }

    counter_ += deltaTime;
    float t = std::clamp(counter_ / duration_, 0.0f, 1.0f);
    PostEffect::Params* params = GetPostEffectParams();
    if (!params) {
        return;
    }

    if (status_ == Status::FadeOut) {
        params->slimeFadeIntensity = t;
    } else if (status_ == Status::FadeIn) {
        params->slimeFadeIntensity = 1.0f - t;
    } else if (status_ == Status::IrisOut) {
        params->irisFadeIntensity = t;
    } else if (status_ == Status::IrisIn) {
        params->irisFadeIntensity = 1.0f - t;
    }

    if (t >= 1.0f) {
        status_ = Status::Finished;
    }
}

void Fade::StartFadeIn(float duration) {
    status_ = Status::FadeIn;
    duration_ = duration;
    counter_ = 0.0f;
    if (PostEffect::Params* params = GetPostEffectParams()) {
        params->slimeFadeIntensity = 1.0f;
    }
}

void Fade::StartFadeOut(float duration) {
    status_ = Status::FadeOut;
    duration_ = duration;
    counter_ = 0.0f;
    if (PostEffect::Params* params = GetPostEffectParams()) {
        params->slimeFadeIntensity = 0.0f;
    }
}

void Fade::Stop() {
    status_ = Status::None;
    if (PostEffect::Params* params = GetPostEffectParams()) {
        params->slimeFadeIntensity = 0.0f;
        params->irisFadeIntensity = 0.0f;
    }
}

void Fade::StartIrisOut(float duration, const Vector2& center) {
    status_ = Status::IrisOut;
    duration_ = duration;
    counter_ = 0.0f;
    if (PostEffect::Params* params = GetPostEffectParams()) {
        params->irisFadeIntensity = 0.0f;
        params->irisCenterX = center.x;
        params->irisCenterY = center.y;
    }
}

void Fade::StartIrisIn(float duration, const Vector2& center) {
    status_ = Status::IrisIn;
    duration_ = duration;
    counter_ = 0.0f;
    if (PostEffect::Params* params = GetPostEffectParams()) {
        params->irisFadeIntensity = 1.0f;
        params->irisCenterX = center.x;
        params->irisCenterY = center.y;
    }
}

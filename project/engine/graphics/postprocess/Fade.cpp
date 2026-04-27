#include "Fade.h"
#include "PostEffect.h"
#include "engine/graphics/core/TextureManager.h"
#include <algorithm>

Fade* Fade::GetInstance() {
    static Fade instance;
    return &instance;
}

void Fade::Initialize() {
    // スライムフェード用のノイズテクスチャをロードしてセット
    uint32_t noiseHandle = TextureManager::GetInstance()->Load("Resources/sprite/noise0.png");
    PostEffect::GetInstance()->SetNoiseTexture(noiseHandle);
}

void Fade::Update(float deltaTime) {
    if (status_ == Status::None || status_ == Status::Finished) {
        return;
    }

    counter_ += deltaTime;
    float t = std::clamp(counter_ / duration_, 0.0f, 1.0f);

    if (status_ == Status::FadeOut) {
        PostEffect::GetInstance()->GetParams()->slimeFadeIntensity = t;
    } else if (status_ == Status::FadeIn) {
        PostEffect::GetInstance()->GetParams()->slimeFadeIntensity = 1.0f - t;
    } else if (status_ == Status::IrisOut) {
        PostEffect::GetInstance()->GetParams()->irisFadeIntensity = t;
    } else if (status_ == Status::IrisIn) {
        PostEffect::GetInstance()->GetParams()->irisFadeIntensity = 1.0f - t;
    }

    if (t >= 1.0f) {
        status_ = Status::Finished;
    }
}

void Fade::StartFadeIn(float duration) {
    status_ = Status::FadeIn;
    duration_ = duration;
    counter_ = 0.0f;
    PostEffect::GetInstance()->GetParams()->slimeFadeIntensity = 1.0f;
}

void Fade::StartFadeOut(float duration) {
    status_ = Status::FadeOut;
    duration_ = duration;
    counter_ = 0.0f;
    PostEffect::GetInstance()->GetParams()->slimeFadeIntensity = 0.0f;
}

void Fade::Stop() {
    status_ = Status::None;
    PostEffect::GetInstance()->GetParams()->slimeFadeIntensity = 0.0f;
    PostEffect::GetInstance()->GetParams()->irisFadeIntensity = 0.0f;
}

void Fade::StartIrisOut(float duration, const Vector2& center) {
    status_ = Status::IrisOut;
    duration_ = duration;
    counter_ = 0.0f;
    PostEffect::GetInstance()->GetParams()->irisFadeIntensity = 0.0f;
    PostEffect::GetInstance()->GetParams()->irisCenterX = center.x;
    PostEffect::GetInstance()->GetParams()->irisCenterY = center.y;
}

void Fade::StartIrisIn(float duration, const Vector2& center) {
    status_ = Status::IrisIn;
    duration_ = duration;
    counter_ = 0.0f;
    PostEffect::GetInstance()->GetParams()->irisFadeIntensity = 1.0f;
    PostEffect::GetInstance()->GetParams()->irisCenterX = center.x;
    PostEffect::GetInstance()->GetParams()->irisCenterY = center.y;
}

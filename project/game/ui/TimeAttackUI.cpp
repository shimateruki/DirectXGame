#include "TimeAttackUI.h"
#include "TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>

namespace {
constexpr float kBaseDigitSize = 64.0f;
constexpr float kDigitPopDuration = 0.16f;
constexpr std::array<float, 8> kDigitOffsets = {
    0.0f,
    42.0f,
    90.0f,
    106.0f,
    148.0f,
    200.0f,
    216.0f,
    258.0f
};

float EaseOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}
}

void TimeAttackUI::Initialize(SpriteCommon* spriteCommon) {
    spriteCommon_ = spriteCommon;
    ResetDigitMotion();

    for (int i = 0; i < 10; ++i) {
        const std::string path = "Resources/sprite/number/" + std::to_string(i) + ".png";
        numberTexHandles_[i] = TextureManager::GetInstance()->Load(path);
    }
    colonTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/number/colon.png");
    dotTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/number/dot.png");

    digitSprites_.clear();
    digitSprites_.reserve(8);
    for (int i = 0; i < 8; ++i) {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_, numberTexHandles_[0]);

        if (i == 2) {
            sprite->SetTextureHandle(colonTexHandle_);
        } else if (i == 5) {
            sprite->SetTextureHandle(dotTexHandle_);
        }

        digitSprites_.push_back(std::move(sprite));
    }

    ApplyLayout();
    ApplyColor();
}

void TimeAttackUI::Reset() {
    currentTime_ = 0.0f;
    isRunning_ = false;
    isRolling_ = false;
    isCountingUp_ = false;
    ResetDigitMotion();
}

void TimeAttackUI::SetTime(float time) {
    currentTime_ = (std::max)(0.0f, time);
    isRunning_ = false;
    isRolling_ = false;
    isCountingUp_ = false;
    ResetDigitMotion();
}

void TimeAttackUI::StartRollEffect() {
    isRolling_ = true;
    isCountingUp_ = false;
    rollTimer_ = 0.0f;
    fixedDigitCount_ = 0;
}

void TimeAttackUI::StartCountUp(float targetTime, float duration) {
    countUpStartTime_ = 0.0f;
    countUpTargetTime_ = (std::max)(0.0f, targetTime);
    countUpDuration_ = (std::max)(0.01f, duration);
    countUpTimer_ = 0.0f;
    currentTime_ = countUpStartTime_;
    isRunning_ = false;
    isRolling_ = false;
    isCountingUp_ = true;
    ResetDigitMotion();
}

void TimeAttackUI::Update(float deltaTime) {
    if (isRunning_) {
        currentTime_ += deltaTime;
    }

    if (isCountingUp_) {
        countUpTimer_ += deltaTime;
        const float rate = std::clamp(countUpTimer_ / countUpDuration_, 0.0f, 1.0f);
        const float eased = EaseOutCubic(rate);
        currentTime_ = countUpStartTime_ + (countUpTargetTime_ - countUpStartTime_) * eased;
        if (rate >= 1.0f) {
            currentTime_ = countUpTargetTime_;
            isCountingUp_ = false;
        }
    }

    UpdateDigitTextures(deltaTime);
    ApplyLayout();
    ApplyColor();

    for (auto& sprite : digitSprites_) {
        if (sprite) {
            sprite->Update();
        }
    }
}

void TimeAttackUI::UpdateDigitTextures(float deltaTime) {
    const float displayTime = (std::max)(0.0f, currentTime_);
    const int minutes = static_cast<int>(displayTime / 60.0f);
    const int seconds = static_cast<int>(std::fmod(displayTime, 60.0f));
    const int ms = static_cast<int>(std::fmod(displayTime, 1.0f) * 100.0f);

    std::array<int, 6> targetDigits{};
    targetDigits[0] = (minutes / 10) % 10;
    targetDigits[1] = minutes % 10;
    targetDigits[2] = (seconds / 10) % 10;
    targetDigits[3] = seconds % 10;
    targetDigits[4] = (ms / 10) % 10;
    targetDigits[5] = ms % 10;

    std::array<int, 6> displayDigits = targetDigits;
    if (isRolling_) {
        rollTimer_ += deltaTime;
        if (rollTimer_ > 0.12f) {
            rollTimer_ -= 0.12f;
            fixedDigitCount_++;
            if (fixedDigitCount_ >= 6) {
                isRolling_ = false;
                fixedDigitCount_ = 6;
            }
        }

        for (int i = 0; i < 6; ++i) {
            if (i >= fixedDigitCount_) {
                displayDigits[i] = std::rand() % 10;
            }
        }
    }

    if (digitSprites_.size() < 8) {
        return;
    }

    constexpr std::array<int, 6> spriteIndices = { 0, 1, 3, 4, 6, 7 };
    for (int i = 0; i < 6; ++i) {
        const int spriteIndex = spriteIndices[i];
        if (previousDisplayDigits_[i] != displayDigits[i]) {
            digitPopTimers_[spriteIndex] = kDigitPopDuration;
            previousDisplayDigits_[i] = displayDigits[i];
        }
    }

    digitSprites_[0]->SetTextureHandle(numberTexHandles_[displayDigits[0]]);
    digitSprites_[1]->SetTextureHandle(numberTexHandles_[displayDigits[1]]);
    digitSprites_[3]->SetTextureHandle(numberTexHandles_[displayDigits[2]]);
    digitSprites_[4]->SetTextureHandle(numberTexHandles_[displayDigits[3]]);
    digitSprites_[6]->SetTextureHandle(numberTexHandles_[displayDigits[4]]);
    digitSprites_[7]->SetTextureHandle(numberTexHandles_[displayDigits[5]]);

    for (float& timer : digitPopTimers_) {
        timer = (std::max)(0.0f, timer - deltaTime);
    }
}

void TimeAttackUI::Draw() {
    for (auto& sprite : digitSprites_) {
        if (sprite) {
            sprite->Draw();
        }
    }
}

void TimeAttackUI::SetPosition(const Vector2& basePos) {
    SetPosition(basePos, spacingScale_);
}

void TimeAttackUI::SetPosition(const Vector2& basePos, float spacingScale) {
    basePosition_ = basePos;
    spacingScale_ = spacingScale;
    ApplyLayout();
}

void TimeAttackUI::SetScale(float scale) {
    digitScale_ = (std::max)(0.01f, scale);
    ApplyLayout();
}

void TimeAttackUI::SetAlpha(float alpha) {
    color_.w = std::clamp(alpha, 0.0f, 1.0f);
    ApplyColor();
}

void TimeAttackUI::SetColor(const Vector4& color) {
    color_ = color;
    ApplyColor();
}

void TimeAttackUI::ApplyLayout() {
    for (int i = 0; i < 8; ++i) {
        if (i < static_cast<int>(digitSprites_.size()) && digitSprites_[i]) {
            float digitMotion = 0.0f;
            if (digitPopTimers_[i] > 0.0f) {
                const float progress = 1.0f - std::clamp(digitPopTimers_[i] / kDigitPopDuration, 0.0f, 1.0f);
                digitMotion = std::sin(progress * 3.14159265f);
            }

            const float posX = basePosition_.x + (kDigitOffsets[i] * spacingScale_);
            const float animatedScale = digitScale_ * (1.0f + 0.06f * digitMotion);
            digitSprites_[i]->SetPosition({ posX, basePosition_.y - 8.0f * digitMotion });
            digitSprites_[i]->SetSize({ kBaseDigitSize * animatedScale, kBaseDigitSize * animatedScale });
        }
    }
}

void TimeAttackUI::ApplyColor() {
    for (auto& sprite : digitSprites_) {
        if (sprite) {
            sprite->SetColor(color_);
        }
    }
}

void TimeAttackUI::ResetDigitMotion() {
    digitPopTimers_.fill(0.0f);
    previousDisplayDigits_.fill(-1);
}

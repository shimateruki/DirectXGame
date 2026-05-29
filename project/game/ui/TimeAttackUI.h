#pragma once
#include "Sprite.h"
#include <array>
#include <memory>
#include <vector>

class SpriteCommon;

class TimeAttackUI {
public:
    void Initialize(SpriteCommon* spriteCommon);
    void Update(float deltaTime);
    void Draw();

    void Start() { isRunning_ = true; }
    void Stop() { isRunning_ = false; }
    void Reset();
    void SetTime(float time);
    float GetCurrentTime() const { return currentTime_; }

    void SetAlpha(float alpha);
    void SetColor(const Vector4& color);
    void SetScale(float scale);
    void SetPosition(const Vector2& basePos);
    void SetPosition(const Vector2& basePos, float spacingScale);

    void StartRollEffect();
    void StartCountUp(float targetTime, float duration);
    bool IsRolling() const { return isRolling_; }
    bool IsCountingUp() const { return isCountingUp_; }
    bool IsAnimating() const { return isRolling_ || isCountingUp_; }

private:
    void ApplyLayout();
    void ApplyColor();
    void UpdateDigitTextures(float deltaTime);
    void ResetDigitMotion();

    std::array<uint32_t, 10> numberTexHandles_{};
    uint32_t colonTexHandle_ = 0;
    uint32_t dotTexHandle_ = 0;

    std::vector<std::unique_ptr<Sprite>> digitSprites_;
    std::array<float, 8> digitPopTimers_{};
    std::array<int, 6> previousDisplayDigits_ = { -1, -1, -1, -1, -1, -1 };

    float currentTime_ = 0.0f;
    bool isRunning_ = false;
    SpriteCommon* spriteCommon_ = nullptr;

    bool isRolling_ = false;
    float rollTimer_ = 0.0f;
    int fixedDigitCount_ = 0;

    bool isCountingUp_ = false;
    float countUpStartTime_ = 0.0f;
    float countUpTargetTime_ = 0.0f;
    float countUpTimer_ = 0.0f;
    float countUpDuration_ = 1.0f;

    Vector2 basePosition_ = { 1200.0f, 50.0f };
    float spacingScale_ = 1.0f;
    float digitScale_ = 1.0f;
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
};

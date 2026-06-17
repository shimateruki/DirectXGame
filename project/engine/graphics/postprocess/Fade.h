#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Sprite;
class SpriteCommon;

class Fade {
public:
    enum class Status {
        None,
        FadeIn,
        FadeOut,
        IrisIn,
        IrisOut,
        Finished
    };

    static Fade* GetInstance();

    void Initialize();
    void Update(float deltaTime);
    void Draw();

    void StartFadeIn(float duration);
    void StartFadeOut(float duration);
    void StartIrisOut(float duration, const Vector2& center = { 0.5f, 0.5f });
    void StartIrisIn(float duration, const Vector2& center = { 0.5f, 0.5f });

    bool IsFinished() const { return status_ == Status::Finished; }
    Status GetStatus() const { return status_; }

    void Stop();

private:
    enum class VisualStyle {
        SlimeWipe,
        CrownIris
    };

    struct FrameSequence {
        std::unique_ptr<Sprite> sprite;
        std::vector<uint32_t> textureHandles;
    };

    Fade() = default;
    ~Fade() = default;
    Fade(const Fade&) = delete;
    Fade& operator=(const Fade&) = delete;

    void InitializeSprites();
    void LoadSequence(FrameSequence& sequence, const std::string& directory, int frameCount);
    void ResetPostEffectFade();
    void Begin(Status status, VisualStyle style, float duration, const Vector2& center, float initialCoverage);

    void DrawFrameSequence(FrameSequence& sequence, float coverage);
    void DrawFallbackBlack(float alpha);

    float GetNormalizedTime() const;
    bool IsClosing() const;

private:
    Status status_ = Status::None;
    VisualStyle style_ = VisualStyle::SlimeWipe;

    float duration_ = 1.0f;
    float counter_ = 0.0f;
    float coverage_ = 0.0f;
    Vector2 center_ = { 0.5f, 0.5f };

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> fallbackBlack_;
    FrameSequence slimeWipe_;
    FrameSequence crownIris_;
};

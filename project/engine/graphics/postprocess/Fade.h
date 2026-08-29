#pragma once

#include "engine/utility/math/Math.h"

#include <memory>

class Sprite;
class SpriteCommon;

/// 画面全体を覆う汎用フェードイン・フェードアウトを管理します。
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
    Fade() = default;
    ~Fade() = default;
    Fade(const Fade&) = delete;
    Fade& operator=(const Fade&) = delete;

    void InitializeSprites();
    void ResolveNoiseTexture();
    void ResetPostEffectFade();
    void Begin(
        Status status,
        float duration,
        const Vector2& center,
        float initialCoverage);
    void DrawFallbackBlack(float alpha);

    float GetNormalizedTime() const;
    bool IsClosing() const;

private:
    Status status_ = Status::None;
    float duration_ = 1.0f;
    float counter_ = 0.0f;
    float coverage_ = 0.0f;
    Vector2 center_ = { 0.5f, 0.5f };

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> fallbackBlack_;
    bool noiseTextureResolved_ = false;
};

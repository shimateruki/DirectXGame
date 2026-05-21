#pragma once
#include "Sprite.h"
#include "engine/utility/math/Math.h"
#include <memory>

class CinematicFade {
public:
    enum class State {
        kIdle,      // 待機中（全開）
        kClosing,   // 閉じてる途中
        kClosed,    // 完全に閉まった状態
        kOpening    // 開いてる途中
    };

    static CinematicFade* GetInstance();

    void Initialize(SpriteCommon* spriteCommon);

    void Update(float deltaTime);
    void Draw();

    void SetSpriteCommon(SpriteCommon* common);

    void StartClose(float duration = 0.5f);
    void StartOpen(float duration = 0.5f);

    State GetState() const { return state_; }
    bool IsClosed() const { return state_ == State::kClosed; }
    void ForceOpen();

private:
    CinematicFade() = default;
    ~CinematicFade() = default;
    CinematicFade(const CinematicFade&) = delete;
    CinematicFade& operator=(const CinematicFade&) = delete;

    State state_ = State::kIdle;

    std::unique_ptr<Sprite> topSprite_;
    std::unique_ptr<Sprite> bottomSprite_;

    float timer_ = 0.0f;
    float duration_ = 0.5f;

    float screenWidth_ = 1280.0f;
    float screenHeight_ = 720.0f;
};
#pragma once
#include <cstdint>
#include "engine/utility/math/Math.h"

/// <summary>
/// スライムフェードを管理するクラス
/// </summary>
class Fade {
public:
    enum class Status {
        None,
        FadeIn,   // スライムフェードイン
        FadeOut,  // スライムフェードアウト
        IrisIn,   // アイリスイン
        IrisOut,  // アイリスアウト
        Finished
    };

    static Fade* GetInstance();

    void Initialize();
    void Update(float deltaTime);

    /// <summary>
    /// フェードイン開始 (画面を表示する)
    /// </summary>
    void StartFadeIn(float duration);

    /// <summary>
    /// フェードアウト開始 (画面をスライムで覆う)
    /// </summary>
    void StartFadeOut(float duration);
    
    /// <summary>
    /// アイリスアウト開始 (丸が閉じる)
    /// </summary>
    void StartIrisOut(float duration, const Vector2& center = {0.5f, 0.5f});
    
    /// <summary>
    /// アイリスイン開始 (丸が開く)
    /// </summary>
    void StartIrisIn(float duration, const Vector2& center = {0.5f, 0.5f});

    bool IsFinished() const { return status_ == Status::Finished; }
    Status GetStatus() const { return status_; }

    void Stop();

private:
    Fade() = default;
    ~Fade() = default;
    Fade(const Fade&) = delete;
    Fade& operator=(const Fade&) = delete;

private:
    Status status_ = Status::None;
    float duration_ = 1.0f;
    float counter_ = 0.0f;
};

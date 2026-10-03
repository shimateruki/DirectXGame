#pragma once

#include "engine/utility/math/Math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Sprite;
class SpriteCommon;

/// <summary>画面全体を覆うフェードとアイリス演出を管理する。</summary>
class Fade {
public:
    /// <summary>現在進行中の画面遷移。</summary>
    enum class Status {
        None,
        FadeIn,
        FadeOut,
        IrisIn,
        IrisOut,
        Finished
    };

    /// <summary>共有の画面遷移インスタンスを取得する。</summary>
    static Fade* GetInstance();

    /// <summary>画面を覆うSpriteと必要な素材を初期化する。</summary>
    void Initialize();

    /// <summary>指定秒数だけ演出を進める。</summary>
    void Update(float deltaTime);

    /// <summary>現在のフェード画面を描画する。</summary>
    void Draw();

    /// <summary>画面を開くフェードを開始する。</summary>
    void StartFadeIn(float duration);

    /// <summary>画面を閉じるフェードを開始する。</summary>
    void StartFadeOut(float duration);

    /// <summary>指定中心へ閉じるアイリス演出を開始する。</summary>
    void StartIrisOut(float duration, const Vector2& center = { 0.5f, 0.5f });

    /// <summary>指定中心から開くアイリス演出を開始する。</summary>
    void StartIrisIn(float duration, const Vector2& center = { 0.5f, 0.5f });

    /// <summary>演出が完了したかを返す。</summary>
    bool IsFinished() const { return status_ == Status::Finished; }

    /// <summary>現在の演出状態を返す。</summary>
    Status GetStatus() const { return status_; }

    /// <summary>進行中の演出を停止する。</summary>
    void Stop();

private:
    /// <summary>フェードに使用する画像系列の種類。</summary>
    enum class VisualStyle {
        SlimeWipe,
        CrownIris
    };

    /// <summary>連番テクスチャとその描画Sprite。</summary>
    struct FrameSequence {
        std::unique_ptr<Sprite> sprite;
        std::vector<uint32_t> textureHandles;
    };

    Fade() = default;
    ~Fade() = default;
    Fade(const Fade&) = delete;
    Fade& operator=(const Fade&) = delete;

    // 素材の準備と、ポストエフェクト側に残った値の初期化。
    void InitializeSprites();

    // directory から frameCount 枚の連番画像を読み込む。
    void LoadSequence(FrameSequence& sequence, const std::string& directory, int frameCount);

    // アイリス用ノイズテクスチャの参照を解決する。
    void ResolveNoiseTexture();

    // 遷移後にポストエフェクトのフェード値を既定値へ戻す。
    void ResetPostEffectFade();

    // 開始時の画面被覆率を含め、すべての遷移経路を同じ状態初期化に通す。
    void Begin(Status status, VisualStyle style, float duration, const Vector2& center, float initialCoverage);

    // 被覆率に対応するフレームを描画し、画像がなければ黒画面へフォールバックする。
    void DrawFrameSequence(FrameSequence& sequence, float coverage);
    void DrawFallbackBlack(float alpha);

    // 現在の演出時間を 0～1 に正規化し、閉じる方向の演出かを判定する。
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
    bool noiseTextureResolved_ = false;
};

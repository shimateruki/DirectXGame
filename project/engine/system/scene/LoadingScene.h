#pragma once

#include "BaseScene.h"
#include "Sprite.h"
#include "SpriteCommon.h"

#include <memory>
#include <vector>

/// <summary>
/// シーン切り替え中に進捗、アニメーション、ヒントを表示する専用シーン。
/// </summary>
class LoadingScene : public BaseScene {
public:
    LoadingScene() = default;
    ~LoadingScene() override = default;

    /// <summary>ロード表示に使うSpriteと素材を準備する。</summary>
    void Initialize() override;

    /// <summary>進捗表示とアニメーションを更新する。</summary>
    void Update(float deltaTime) override;

    /// <summary>このシーンでは3D描画を行わない。</summary>
    void Draw() override;

    /// <summary>ロード画面のUIを描画する。</summary>
    void DrawUI() override;

    /// <summary>ロード画面で保持した描画資源を解放する。</summary>
    void Finalize() override;

    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }

    /// <summary>
    /// ロード進捗を0.0から1.0の範囲で設定する。
    /// </summary>
    void SetProgress(float progress);

private:
    // Sprite を生成して所有リストに登録する。
    Sprite* CreateSprite(uint32_t textureHandle, const Vector2& position, const Vector2& size, const Vector4& color);

    // ロード中のスライム・点滅ドット・進捗ゲージをそれぞれ更新する。
    void UpdateSlimeRunAnimation();
    void UpdateDotAnimation();
    void UpdateProgressGauge();

private:
    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    Sprite* background_ = nullptr;
    Sprite* loadingText_ = nullptr;
    Sprite* slime_ = nullptr;
    Sprite* shadow_ = nullptr;
    Sprite* hintText_ = nullptr;
    std::vector<Sprite*> dots_;

    Sprite* gaugeShadow_ = nullptr;
    Sprite* gaugeFrame_ = nullptr;
    Sprite* gaugeTrack_ = nullptr;
    Sprite* gaugeFill_ = nullptr;
    Sprite* gaugeHighlight_ = nullptr;
    Sprite* gaugeGlow_ = nullptr;
    std::vector<Sprite*> gaugeBubbles_;

    float timer_ = 0.0f;
    float targetProgress_ = 0.0f;
    float displayedProgress_ = 0.0f;
    int slimeFrame_ = -1;
};

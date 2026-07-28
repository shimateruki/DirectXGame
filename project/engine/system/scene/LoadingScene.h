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

    void Initialize() override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawUI() override;
    void Finalize() override;

    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }

    /// <summary>
    /// ロード進捗を0.0から1.0の範囲で設定する。
    /// </summary>
    void SetProgress(float progress);

private:
    Sprite* CreateSprite(uint32_t textureHandle, const Vector2& position, const Vector2& size, const Vector4& color);
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

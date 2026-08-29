#pragma once

#include "BaseScene.h"
#include "Sprite.h"
#include "SpriteCommon.h"

#include <memory>
#include <vector>

/// シーン切り替え中に、汎用図形だけで進捗を表示する専用シーンです。
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

    /// ロード進捗を0.0から1.0の範囲で設定します。
    void SetProgress(float progress);

private:
    Sprite* CreateSprite(
        uint32_t textureHandle,
        const Vector2& position,
        const Vector2& size,
        const Vector4& color);
    void UpdateMarkerAnimation();
    void UpdateDotAnimation();
    void UpdateProgressGauge();

private:
    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    Sprite* background_ = nullptr;
    Sprite* marker_ = nullptr;
    Sprite* markerShadow_ = nullptr;
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
};

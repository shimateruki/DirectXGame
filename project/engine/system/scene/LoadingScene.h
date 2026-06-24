#pragma once
#include "BaseScene.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
#include <vector>

/// <summary>
/// シーン切り替え中に表示するロード画面。
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
    /// ロード進捗を0.0から1.0で設定する。
    /// </summary>
    void SetProgress(float progress);

private:
    Sprite* CreateSprite(uint32_t textureHandle, const Vector2& position, const Vector2& size, const Vector4& color);
    void UpdateSlimeRunAnimation();
    void UpdateDotAnimation();
    void UpdateProgressSprites();
    void LayoutProgressSprites(int percent);

private:
    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    Sprite* background_ = nullptr;
    Sprite* loadingText_ = nullptr;
    Sprite* slime_ = nullptr;
    Sprite* shadow_ = nullptr;
    Sprite* hintText_ = nullptr;
    std::vector<Sprite*> dots_;
    std::vector<Sprite*> progressDigits_;
    Sprite* progressSlash_ = nullptr;
    Sprite* progressDotTop_ = nullptr;
    Sprite* progressDotBottom_ = nullptr;
    uint32_t digitTextureHandles_[10] = {};

    float timer_ = 0.0f;
    float targetProgress_ = 0.0f;
    float displayedProgress_ = 0.0f;
    int lastDisplayedPercent_ = -1;
};

#pragma once
#include "BaseScene.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
#include <vector>

/// <summary>
/// シーン切り替え中に表示するロード画面。
/// </summary>
// LoadingSceneは、シーン切り替え中の進捗、アニメーション、ヒント表示を担当する専用シーンです。
class LoadingScene : public BaseScene {
public:
    LoadingScene() = default;
    ~LoadingScene() override = default;

        // ローディング画面用Spriteとテクスチャを準備します。
void Initialize() override;
        // スライム走り、ドット点滅、表示進捗の追従を更新します。
void Update(float deltaTime) override;
    void Draw() override;
        // ローディング画面の2D UIを描画します。
void DrawUI() override;
    void Finalize() override;

    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }

    /// <summary>
    /// ロード進捗を0.0から1.0で設定する。
    /// </summary>
        // 外部から読み込み進捗を受け取り、表示用の目標値へ反映します。
void SetProgress(float progress);

private:
        // ローディング画面用Spriteを生成し、内部リストへ登録します。
Sprite* CreateSprite(uint32_t textureHandle, const Vector2& position, const Vector2& size, const Vector4& color);
    void UpdateSlimeRunAnimation();
    void UpdateDotAnimation();
        // 数字Spriteを現在の表示進捗に合わせて並べ直します。
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

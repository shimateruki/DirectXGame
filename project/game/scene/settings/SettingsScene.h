#pragma once
#include "AudioPlayer.h"
#include "BaseScene.h"
#include "SpriteCommon.h"
#include "game/ui/SettingsMenuOverlay.h"
#include <memory>

// 独立設定シーンもゲーム中と同じUI・適用ルールを使います。
class SettingsScene : public BaseScene {
  public:
    /// 設定メニューのスプライトと操作状態を準備する。
    void Initialize() override;
    /// メニュー表示に必要な画像の事前読み込み対象を返す。
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    /// 設定メニューが保持する表示資源を解放する。
    void Finalize() override;
    /// 入力に応じて設定項目と画面遷移を更新する。
    void Update(float deltaTime) override;
    void Draw() override {}
    /// 設定画面のスプライトを描画する。
    void DrawUI() override;
    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }
    engine::SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    std::string GetName() override { return "Settings"; }

  private:
    std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() override { return &sprites_; }
    std::unique_ptr<engine::SpriteCommon> spriteCommon_;
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;
    SettingsMenuOverlay menu_;
};

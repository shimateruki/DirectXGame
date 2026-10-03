#pragma once
#include "AudioPlayer.h"
#include "BaseScene.h"
#include "SpriteCommon.h"
#include "game/ui/SettingsMenuOverlay.h"
#include <memory>

// 独立設定シーンもゲーム中と同じUI・適用ルールを使います。
class SettingsScene : public BaseScene {
public:
    void Initialize() override;
    SceneLoadManifest BuildAsyncLoadManifest() const override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void Draw() override {}
    void DrawUI() override;
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    std::string GetName() override { return "Settings"; }
private:
    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::vector<std::unique_ptr<Sprite>> sprites_;
    SettingsMenuOverlay menu_;
    AudioPlayer::AudioHandle bgmHandle_ = AudioPlayer::kInvalidAudioHandle;
};

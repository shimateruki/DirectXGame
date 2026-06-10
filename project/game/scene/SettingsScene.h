#pragma once

#include "AudioPlayer.h"
#include "BaseScene.h"
#include "SpriteCommon.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

class DirectXCommon;
class InputManager;
class LevelLoader;

class SettingsScene : public BaseScene {
public:
    SettingsScene() = default;
    ~SettingsScene() override = default;

    void Initialize() override;
    void Finalize() override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawUI() override;

    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    std::string GetName() override { return "Settings"; }

private:
    enum class Item {
        BGM,
        SE,
        CameraSensitivity,
        Count
    };

    struct OptionRow {
        Sprite* backdrop = nullptr;
        Sprite* label = nullptr;
        Sprite* track = nullptr;
        Sprite* fill = nullptr;
        Sprite* knob = nullptr;
        std::array<Sprite*, 3> digits = { nullptr, nullptr, nullptr };
    };

    void InitializeSprites();
    void BindLayoutSprites();
    Sprite* FindSprite(const std::string& name) const;
    void UpdateInput(float deltaTime);
    void UpdateSprites(float deltaTime);
    void ChangeSelection(int direction);
    void AdjustSelectedValue(int direction);
    void ReturnToTitle();
    float GetValue(Item item) const;
    float GetNormalizedValue(Item item) const;
    int GetDisplayValue(Item item) const;
    void SetNumberSprites(OptionRow& row, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color);

    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    Sprite* background_ = nullptr;
    Sprite* panel_ = nullptr;
    Sprite* title_ = nullptr;
    Sprite* hintLine_ = nullptr;
    std::array<OptionRow, static_cast<size_t>(Item::Count)> rows_{};

    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
    float repeatTimer_ = 0.0f;
    int repeatDirection_ = 0;
    AudioPlayer::AudioHandle bgmHandle_ = AudioPlayer::kInvalidAudioHandle;
};

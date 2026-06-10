#pragma once

#include "AudioPlayer.h"
#include "engine/utility/math/Math.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

class InputManager;
class Sprite;
class SpriteCommon;

class SettingsMenuOverlay {
public:
    enum class Result {
        None,
        Closed
    };

    void Initialize(SpriteCommon* spriteCommon, const std::string& layoutPath = "Resources/json/sprite/settingsScene.json");
    void Finalize();
    void SetActive(bool active);
    bool IsActive() const { return isActive_; }
    Result Update(float deltaTime);
    void Draw();

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

    void LoadLayout(const std::string& layoutPath);
    void BindLayoutSprites();
    Sprite* FindSprite(const std::string& name) const;
    void SetAllVisible(bool visible);
    bool IsDecisionOrBackHeld() const;
    Result UpdateInput(float deltaTime);
    void UpdateSprites();
    void ChangeSelection(int direction);
    void AdjustSelectedValue(int direction);
    float GetValue(Item item) const;
    float GetNormalizedValue(Item item) const;
    int GetDisplayValue(Item item) const;
    void SetNumberSprites(OptionRow& row, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color);

    SpriteCommon* spriteCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    Sprite* background_ = nullptr;
    Sprite* panel_ = nullptr;
    Sprite* title_ = nullptr;
    Sprite* hintLine_ = nullptr;
    std::array<OptionRow, static_cast<size_t>(Item::Count)> rows_{};

    bool isActive_ = false;
    bool suppressCloseInput_ = false;
    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
    float repeatTimer_ = 0.0f;
    int repeatDirection_ = 0;
};

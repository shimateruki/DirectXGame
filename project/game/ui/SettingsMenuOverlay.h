#pragma once

#include "AudioPlayer.h"
#include "engine/utility/math/Math.h"

#include <array>
#include <memory>
#include <string>
#include <vector>
#include "json.hpp"

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
    void CollectReplaySprites(std::vector<Sprite*>& sprites) const;
    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);

private:
    enum class Category {
        Sound,
        Control,
        Count
    };

    enum class Item {
        BGM,
        SE,
        CameraSensitivity,
        Count
    };

    enum class FocusArea {
        Items,
        Footer
    };

    enum class FooterAction {
        Back,
        Reset,
        Apply,
        Count
    };

    struct OptionRow {
        Sprite* backdrop = nullptr;
        Sprite* label = nullptr;
        Sprite* track = nullptr;
        Sprite* fill = nullptr;
        Sprite* knob = nullptr;
        Sprite* leftArrow = nullptr;
        Sprite* rightArrow = nullptr;
        Sprite* valueLabel = nullptr;
        std::array<Sprite*, 3> digits = { nullptr, nullptr, nullptr };
    };

    void LoadLayout(const std::string& layoutPath);
    void BindLayoutSprites();
    Sprite* FindSprite(const std::string& name) const;
    void SetAllVisible(bool visible);
    bool IsDecisionOrBackHeld() const;
    Result UpdateInput(float deltaTime);
    void UpdateSprites();
    void UpdateCategorySprites();
    void UpdateFooterSprites();
    void SetRowVisible(OptionRow& row, bool visible);
    void ChangeSelection(int direction);
    void ChangeFooterSelection(int direction);
    void AdjustSelectedValue(int direction);
    bool ExecuteFooterAction();
    void ResetSettingsToDefault();
    Category GetItemCategory(Item item) const;
    bool IsItemVisible(Item item) const;
    bool IsSliderItem(Item item) const;
    bool IsChoiceItem(Item item) const;
    float GetValue(Item item) const;
    float GetNormalizedValue(Item item) const;
    int GetDisplayValue(Item item) const;
    std::string GetDisplayTextTexture(Item item) const;
    void SetNumberSprites(OptionRow& row, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color);

    SpriteCommon* spriteCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    Sprite* background_ = nullptr;
    Sprite* panel_ = nullptr;
    Sprite* title_ = nullptr;
    Sprite* hintLine_ = nullptr;
    Sprite* slimeCursor_ = nullptr;
    Vector2 slimeCursorOffset_ = { 0.0f, 0.0f };
    Vector2 slimeCursorBaseSize_ = { 0.0f, 0.0f };
    std::array<OptionRow, static_cast<size_t>(Item::Count)> rows_{};
    std::array<Sprite*, static_cast<size_t>(Category::Count)> categoryTabs_ = {};
    std::array<Sprite*, static_cast<size_t>(Category::Count)> categoryIcons_ = {};
    std::array<Sprite*, static_cast<size_t>(Category::Count)> categoryLabels_ = {};
    std::array<Sprite*, static_cast<size_t>(FooterAction::Count)> footerButtons_ = { nullptr, nullptr, nullptr };
    std::array<Sprite*, static_cast<size_t>(FooterAction::Count)> footerTexts_ = { nullptr, nullptr, nullptr };

    bool isActive_ = false;
    bool suppressCloseInput_ = false;
    FocusArea focusArea_ = FocusArea::Items;
    int selectedIndex_ = 0;
    int footerIndex_ = static_cast<int>(FooterAction::Apply);
    float sceneTime_ = 0.0f;
    float repeatTimer_ = 0.0f;
    int repeatDirection_ = 0;
};

#pragma once

#include "engine/utility/math/Math.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

class InputManager;
class Sprite;
class SpriteCommon;

class PauseMenuOverlay {
public:
    enum class Action {
        None,
        Resume,
        Retry,
        OpenSettings,
        ReturnTitle
    };

    void Initialize(SpriteCommon* spriteCommon, const std::string& layoutPath = "Resources/json/sprite/pauseMenu.json");
    void Finalize();
    void SetActive(bool active);
    bool IsActive() const { return isActive_; }
    Action Update(float deltaTime);
    void Draw();

private:
    enum class Item {
        Retry,
        Settings,
        Title,
        Count
    };

    struct MenuRow {
        Sprite* backdrop = nullptr;
        Sprite* label = nullptr;
        Vector2 backdropBaseSize = { 0.0f, 0.0f };
        Vector2 labelBaseSize = { 0.0f, 0.0f };
    };

    void LoadLayout(const std::string& layoutPath);
    void BindLayoutSprites();
    Sprite* FindSprite(const std::string& name) const;
    void SetAllVisible(bool visible);
    bool IsOpenOrCloseHeld() const;
    Action UpdateInput();
    void UpdateSprites();
    void ChangeSelection(int direction);

    SpriteCommon* spriteCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    Sprite* background_ = nullptr;
    Sprite* panel_ = nullptr;
    Sprite* title_ = nullptr;
    std::array<MenuRow, static_cast<size_t>(Item::Count)> rows_{};

    bool isActive_ = false;
    bool suppressOpenCloseInput_ = false;
    int selectedIndex_ = 0;
    float time_ = 0.0f;
};

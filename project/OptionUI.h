#pragma once
#include "Sprite.h"
#include "InputManager.h"
#include <string>
#include <vector>
#include <memory> 

class BaseScene;
class SpriteCommon;

class OptionUI {
public:
    OptionUI() = default;
    ~OptionUI() = default;

    void Initialize(BaseScene* scene, SpriteCommon* spriteCommon);
    bool Update(float deltaTime);
    bool IsOptionSprite(Sprite* sp) const;

    void DrawKeyIcons();

private:
    bool IsValidKey(int keyCode) const;
    std::string GetKeySpriteName(int keyCode) const;
    void RefreshKeyIcons();

private:
    enum class MenuState { Top, KeyConfig, WaitInput };
    MenuState currentState_ = MenuState::Top;

    enum class OptionIndex { Sound, KeyConfig, Max };
    int currentOptionIndex_ = (int)OptionIndex::Sound;

    std::vector<std::string> configActions_ = {
            "Forward", "Backward", "Right", "Left", "Jump", "Dash", "LockOn", "Attack"
    };
    int currentConfigIndex_ = 0;

    // --- エディタで配置したスプライト群 ---
    Sprite* bgSprite_ = nullptr;
    Sprite* titleSprite_ = nullptr;
    Sprite* soundSprite_ = nullptr;
    Sprite* keyboardSprite_ = nullptr;
    Sprite* spaceIconSprite_ = nullptr;
    Sprite* cursorSprite_ = nullptr;
    Vector2 cursorBasePos_ = { 0.0f, 0.0f };

    // ==========================================
    // ★ 目印用（エディタ配置）と 実体（手動生成）
    // ==========================================
    std::vector<Sprite*> actionSprites_; // 座標をもらうだけの目印

    std::vector<std::string> actionSpriteNames_ = {
        "UI/up.png", "UI/down.png", "UI/right.png", "UI/left.png",
        "UI/jump.png", "UI/dahe.png", "UI/Lockon.png", "UI/attck.png"
    };

    std::vector<std::unique_ptr<Sprite>> keyIconSprites_; // 実際に描画する綺麗な画像
    SpriteCommon* spriteCommon_ = nullptr;
};
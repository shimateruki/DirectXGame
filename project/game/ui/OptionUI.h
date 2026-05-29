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
    void Reset();
    bool Update(float deltaTime);
    bool IsOptionSprite(Sprite* sp) const;
    bool IsSpriteVisibleInCurrentTab(Sprite* sp) const;

    void DrawKeyIcons();

private:
    bool IsValidKey(int keyCode) const;
    std::string GetKeySpriteName(int keyCode) const;
    void RefreshKeyIcons();
    void ApplyInputUiIfNeeded();
    void SetSpriteTexturePreserveSize(Sprite* sprite, const std::string& textureName);
    void UpdateSensitivityBar();        // バーの座標を更新する関数
private:
    enum class MenuState { TabSelect, ItemSelect, ValueAdjust, Top, KeyConfig, WaitInput };
    MenuState currentState_ = MenuState::TabSelect;

    enum class TopTab { AudioCamera, Credit, Max };
    int currentTopTab_ = (int)TopTab::AudioCamera;

    enum class SoundOptionIndex { BGM, SE, Camera, Max };
    int currentSoundOptionIndex_ = (int)SoundOptionIndex::BGM;

    void UpdateBGMBar();
    void UpdateSEBar();

    enum class OptionIndex { Sound, KeyConfig, Max };
    int currentOptionIndex_ = (int)OptionIndex::Sound;

    std::vector<std::string> configActions_ = {
            "Forward", "Backward", "Right", "Left", "Jump", "Dash", "LockOn", "Attack"
    };
    int currentConfigIndex_ = 0;

    // --- エディタで配置したスプライト群 ---
    Sprite* optionBackSprite_ = nullptr;
    Sprite* bgSprite_ = nullptr;
    Sprite* titleSprite_ = nullptr;
    Sprite* soundSprite_ = nullptr;
    Sprite* keyboardSprite_ = nullptr;
    Sprite* spaceIconSprite_ = nullptr;
    Sprite* cursorSprite_ = nullptr;
    Vector2 cursorBasePos_ = { 0.0f, 0.0f };

    Sprite* cameraSoundUISprite_ = nullptr;
    Sprite* selectLeftSprite_ = nullptr;
    Sprite* selectRightSprite_ = nullptr;
    Sprite* optionControlSprite_ = nullptr;
    Sprite* optionAIconSprite_ = nullptr;
    Sprite* optionDIconSprite_ = nullptr;
    Vector2 optionAIconBaseSize_ = { 0.0f, 0.0f };
    Vector2 optionDIconBaseSize_ = { 0.0f, 0.0f };
    float tabConfirmBlinkTime_ = 0.0f;
    int confirmedTopTab_ = (int)TopTab::AudioCamera;
    bool optionUiUsesGamepad_ = false;
    bool hasAppliedOptionInputUi_ = false;

    Sprite* bgmSelectSprite_ = nullptr;
    Sprite* seSelectSprite_ = nullptr;
    Sprite* cameraSelectSprite_ = nullptr;
    Sprite* bgmBarSprite_ = nullptr;
    Sprite* seBarSprite_ = nullptr;

    float bgmBarMaxPosX_ = 0.0f;
    float seBarMaxPosX_ = 0.0f;
    float cameraCenterPosX_ = 0.0f;

    // ==========================================
    // 目印用（エディタ配置）と 実体（手動生成）
    // ==========================================
    std::vector<Sprite*> actionSprites_; // 座標をもらうだけの目印

    std::vector<std::string> actionSpriteNames_ = {
        "UI/up.png", "UI/down.png", "UI/right.png", "UI/left.png",
        "UI/jump.png", "UI/dahe.png", "UI/Lockon.png", "UI/attck.png"
    };

    std::vector<std::unique_ptr<Sprite>> keyIconSprites_; // 実際に描画する綺麗な画像
    SpriteCommon* spriteCommon_ = nullptr;
    Sprite* sensitivityBarSprite_ = nullptr;
    Sprite* volumeBarSprite_ = nullptr;         // 音量バーのスプライト（エディタ配置、古い用）
    Sprite* soundConfigCursorSprite_ = nullptr; // サウンド設定画面での項目選択カーソル（エディタ配置、古い用）

    // --- SE ---
    uint32_t seCursorMove_ = 0;
    uint32_t seDecide_ = 0;
    uint32_t seCancel_ = 0;
};

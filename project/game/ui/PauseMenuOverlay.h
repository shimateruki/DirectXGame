#pragma once

#include "engine/utility/math/Math.h"

#include <array>
#include <memory>
#include <string>
#include <vector>
#include "json.hpp"

class InputManager;
class Sprite;
class SpriteCommon;

// JSON 配置のポーズメニューを操作し、確定した項目をゲームシーンへ返します。
class PauseMenuOverlay {
  public:
    enum class Action { None, Resume, Retry, StageSelect, OpenSettings, ReturnTitle };
    enum class Mode { Gameplay, StageSelect };

    void Initialize(SpriteCommon* spriteCommon, const std::string& layoutPath = "Resources/json/sprite/pauseMenu.json",
                    Mode mode = Mode::Gameplay);
    void Finalize();
    void SetActive(bool active);
    bool IsActive() const { return isActive_; }
    /// <summary>入力と開閉アニメーションを更新し、確定した操作を返します。</summary>
    Action Update(float deltaTime);
    void Draw();
    void CollectReplaySprites(std::vector<Sprite*>& sprites) const;
    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);

  private:
    enum class Item { Resume, Retry, StageSelect, Settings, Title, Count };

    // 選択によって背景と文字の表示を一緒に切り替える行です。
    struct MenuRow {
        Sprite* backdrop = nullptr;
        Sprite* label = nullptr;
        Vector2 backdropBaseSize = {0.0f, 0.0f};
        Vector2 labelBaseSize = {0.0f, 0.0f};
    };

    // レイアウト JSON の読み込み後、Sprite を各 UI 要素へ紐付けます。
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
    Sprite* titlePlate_ = nullptr;
    Sprite* title_ = nullptr;
    Sprite* hintBack_ = nullptr;
    Sprite* hintText_ = nullptr;
    Sprite* cursorSlime_ = nullptr;
    Sprite* cursorArrow_ = nullptr;
    std::array<MenuRow, static_cast<size_t>(Item::Count)> rows_{};

    // 開いた直後の同じ入力で閉じないよう、トグル入力を一時抑止します。
    bool isActive_ = false;
    Mode mode_ = Mode::Gameplay;
    bool suppressOpenCloseInput_ = false;
    int selectedIndex_ = 0;
    float time_ = 0.0f;
};

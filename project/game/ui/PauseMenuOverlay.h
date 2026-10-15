#pragma once

#include "engine/utility/math/Math.h"

#include <array>
#include <memory>
#include <string>
#include <vector>
#include "json.hpp"

class InputManager;
namespace engine { class Sprite; }
namespace engine { class SpriteCommon; }

// JSON 配置のポーズメニューを操作し、確定した項目をゲームシーンへ返します。
class PauseMenuOverlay {
  public:
    enum class Action { None, Resume, OpenGuide, Retry, StageSelect, OpenSettings, ReturnTitle };
    enum class Mode { Gameplay, StageSelect };

    void Initialize(engine::SpriteCommon* spriteCommon, const std::string& layoutPath = "Resources/json/sprite/pauseMenu.json",
                    Mode mode = Mode::Gameplay);
    /// ポーズ用 Sprite と操作状態を片付ける。
    void Finalize();
    /// メニューの開閉と選択状態を切り替える。
    void SetActive(bool active);
    /// <summary>操作ガイドから戻った時は、メニューの先頭を選んで表示します。</summary>
    void ReopenFromGuide();
    bool IsActive() const { return isActive_; }
    /// <summary>入力と開閉アニメーションを更新し、確定した操作を返します。</summary>
    Action Update(float deltaTime, bool acceptInput = true);
    // ガイドと共通の背景を二重描画せずにページを移動する。
    void SetGuideTransitionOffset(float offsetX, bool transitioning);
    /// 現在の選択行とカーソルを描画する。
    void Draw();
    /// Replay 対象の Sprite を出力先へ集める。
    void CollectReplaySprites(std::vector<engine::Sprite*>& sprites) const;
    /// 開閉、選択、遷移状態を Replay JSON に記録する。
    void CaptureReplayState(nlohmann::json& state) const;
    /// Replay JSON からポーズ画面の状態を戻す。
    void RestoreReplayState(const nlohmann::json& state);

  private:
    enum class Item { Resume, Guide, Retry, StageSelect, Settings, Title, Count };

    // 選択によって背景と文字の表示を一緒に切り替える行です。
    struct MenuRow {
        engine::Sprite* backdrop = nullptr;
        engine::Sprite* label = nullptr;
        engine::Vector2 backdropBaseSize = {0.0f, 0.0f};
        engine::Vector2 labelBaseSize = {0.0f, 0.0f};
    };

    // レイアウト JSON の読み込み後、Sprite を各 UI 要素へ紐付けます。
    void LoadLayout(const std::string& layoutPath);
    /// JSON 配置で生成した Sprite を各メニュー行へ紐付ける。
    void BindLayoutSprites();
    /// レイアウト上の名前から Sprite を探す。
    engine::Sprite* FindSprite(const std::string& name) const;
    /// メニュー全体の表示状態を設定する。
    void SetAllVisible(bool visible);
    /// 開閉ボタンの押下が継続中か返す。
    bool IsOpenOrCloseHeld() const;
    /// メニュー入力を処理し、確定時の操作を返す。
    Action UpdateInput();
    /// 現在選択とページ移動に合わせて Sprite を配置する。
    void UpdateSprites();
    /// 選択行を指定方向へ移す。
    void ChangeSelection(int direction);

    engine::SpriteCommon* spriteCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;

    engine::Sprite* background_ = nullptr;
    engine::Sprite* panel_ = nullptr;
    engine::Sprite* title_ = nullptr;
    engine::Sprite* closeHintText_ = nullptr;
    engine::Sprite* confirmHintText_ = nullptr;
    engine::Sprite* cursorArrow_ = nullptr;
    engine::Sprite* cursorSlime_ = nullptr;
    engine::Vector2 panelBaseSize_ = {0.0f, 0.0f};
    engine::Vector2 layoutRatio_ = {1.0f, 1.0f};
    std::array<MenuRow, static_cast<size_t>(Item::Count)> rows_{};

    // 開いた直後の同じ入力で閉じないよう、トグル入力を一時抑止します。
    bool isActive_ = false;
    Mode mode_ = Mode::Gameplay;
    bool suppressOpenCloseInput_ = false;
    int selectedIndex_ = 0;
    float time_ = 0.0f;
    float guideTransitionOffset_ = 0.0f;
};

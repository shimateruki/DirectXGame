#pragma once

#include "engine/utility/math/Math.h"
#include "json.hpp"
#include "IEditable.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

class InputManager;
class Player;
namespace engine { class Sprite; }
namespace engine { class SpriteCommon; }

/// <summary>Tabで開く操作ガイド。基本操作とコピー能力の図鑑を表示します。</summary>
class ControlsGuideOverlay : public IEditable {
  public:
    ~ControlsGuideOverlay() override;

    /// 操作ガイドの開発用設定を表示する。
    void DrawImGui() override;
    std::string GetName() override { return "能力ガイド (Ability Guide)"; }

    /// <summary>JSON レイアウトを読み、操作ガイドのスプライトを準備します。</summary>
    /// <param name="spriteCommon">スプライト生成に使う描画基盤。</param>
    /// <param name="player">表示するコピー能力の参照元。</param>
    /// <param name="layoutPath">UI レイアウトの JSON パス。</param>
    void Initialize(engine::SpriteCommon* spriteCommon, Player* player,
                    const std::string& layoutPath = "Resources/json/sprite/controlsGuide.json");
    /// ガイドが所有する Sprite と表示状態を片付ける。
    void Finalize();

    // 開閉時に入力抑制とページ表示を揃えます。
    void SetActive(bool active);
    /// <summary>ポーズ内で使う時は、案内文をメニュー切替に合わせます。</summary>
    void SetPauseMode(bool pauseMode);
    /// <summary>現在のコピー能力のページを最初に表示します。</summary>
    void OpenAbilityPage(bool resetScroll = true);
    bool IsActive() const { return isActive_; }
    /// <summary>能力ページの右端からメニューページへ送る要求を一度だけ受け取ります。</summary>
    bool ConsumeMenuPageRequest();
    void SetPlayer(Player* player) { player_ = player; }

    // 入力、ページ遷移、能力の表示内容を更新します。
    void Update(float deltaTime, bool acceptInput = true);
    // 共通背景を固定し、メニューとの横スライド位置を適用する。
    void SetMenuTransitionOffset(float offsetX, bool transitioning);
    /// 現在ページと図鑑の Sprite を描画する。
    void Draw();

    // UI の開閉・ページ状態をリプレイへ保存します。
    void CollectReplaySprites(std::vector<engine::Sprite*>& sprites) const;
    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);

  private:
    static constexpr int kPageCount = 2;
    static constexpr int kActionCount = 3;

    // 元のレイアウト値を保持し、アニメーション時も JSON の配置に戻せます。
    struct Element {
        engine::Sprite* sprite = nullptr;
        engine::Vector2 basePosition = {0.0f, 0.0f};
        engine::Vector2 baseSize = {0.0f, 0.0f};
        engine::Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    // 基本操作ページと能力ページで共通する Sprite 群です。
    struct PageVisual {
        Element panel;
        Element portrait;
        std::array<Element, kActionCount> actionGlyphs{};
        std::array<Element, kActionCount> actionLabels{};
        std::vector<Element> details;
    };

    // 能力図鑑の一要素。スクロール対象かどうかも保持します。
    struct BookElement {
        Element visual;
        std::string slot;
        bool scroll = false;
        int row = -1;
        engine::Vector2 textureSize = {1.0f, 1.0f};
        engine::Vector2 textureOrigin = {0.0f, 0.0f};
    };

    // 図鑑の内容更新と、スクロール位置に応じた配置を分けます。
    void UpdateBook(float deltaTime);

    /// <summary>図鑑の各要素を、現在のスクロール量とページ移動量から配置します。</summary>
    /// <param name="offsetX">ページ遷移による水平方向の移動量。</param>
    void LayoutBook(float offsetX);
    /// 現在のコピー能力に合わせて図鑑の内容を更新する。
    void RefreshBook();

    // JSON 配置の Sprite を生成し、ページ・矢印・図鑑要素へ紐付けます。
    void LoadLayout(const std::string& layoutPath);
    /// JSON で生成した Sprite をページの表示要素へ紐付ける。
    void BindLayoutSprites();
    /// レイアウト上の名前から Sprite を探す。
    engine::Sprite* FindSprite(const std::string& name) const;
    /// 名前で見つけた Sprite と基準位置を表示要素に保持する。
    Element BindElement(const std::string& name) const;

    // ページ切り替えの入力、スライド、能力表示を管理します。
    void SetAllVisible(bool visible);
    /// 指定ページの表示を切り替える。
    void SetPageVisible(int pageIndex, bool visible);
    /// ページ遷移中の水平位置を反映する。
    void SetPageOffset(int pageIndex, float offsetX);

    /// <summary>現在のページから左右どちらかのページへの移動を開始します。</summary>
    /// <param name="direction">負で左、正で右へ移動します。</param>
    void StartSlide(int direction);
    /// ページ間スライドを進め、完了時に対象ページへ切り替える。
    void UpdateSlide(float deltaTime);
    /// 現在ページの静的な表示状態を更新する。
    void UpdateStaticVisuals();
    /// コピー能力に対応する画像と説明を差し替える。
    void RefreshAbilityVisuals();
    /// 使用中の入力機器に合う操作アイコンへ切り替える。
    void RefreshInputGlyphs(bool force = false);
    /// ガイドの開閉ボタンが押された状態か返す。
    bool IsToggleHeld() const;

    // 外部システムは所有せず、生成したスプライトだけを所有します。
    engine::SpriteCommon* spriteCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    Player* player_ = nullptr;
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;

    Element background_;
    Element leftArrow_;
    Element rightArrow_;
    std::array<Element, kPageCount> pageDots_{};
    std::array<PageVisual, kPageCount> pages_{};
    // 基本操作の配置枠と画像の縦横比を分け、機器切替後も枠の中央へ収める。
    std::array<engine::Vector2, kActionCount> basicGlyphTextureSizes_{};

    // 開閉とページ遷移の一時状態です。時間値は秒単位です。
    bool isActive_ = false;
    bool pauseMode_ = false;
    bool menuPageRequested_ = false;
    bool inputGlyphGamepadMode_ = false;
    std::string inputGlyphBindingSignature_;
    bool suppressToggleInput_ = false;
    bool isSliding_ = false;
    int currentPage_ = 1;
    int outgoingPage_ = 0;
    int incomingPage_ = 0;
    int slideDirection_ = 1;
    float slideTimer_ = 0.0f;
    float slideDuration_ = 0.34f;
    float time_ = 0.0f;
    std::string portraitTexturePath_;
    std::array<std::string, kActionCount> abilityLabelTexturePaths_{};
    std::array<bool, kActionCount> abilityActionAvailable_{};

    // 図鑑の表示状態は通常操作ページのスライド状態と独立しています。
    std::vector<BookElement> bookElements_;
    nlohmann::json bookData_;
    std::string bookKey_;
    bool bookEnabled_ = false;
    bool bookVisible_ = false;
    int bookMoveCount_ = 0;
    float bookScroll_ = 0.0f;
    float bookTargetScroll_ = 0.0f;
    float bookTop_ = 0.0f;
    float bookBottom_ = 0.0f;
    float bookMaxScroll_ = 0.0f;
    float bookScrollSpeed_ = 650.0f;
    float bookWheelStep_ = 125.0f;
    float bookBob_ = 4.0f;
    float bookOffsetX_ = 0.0f;
    std::string layoutPath_;
    engine::Vector2 layoutRatio_ = {1.0f, 1.0f};
    std::string bookPreviewKey_;
    std::string editorStatus_;
};

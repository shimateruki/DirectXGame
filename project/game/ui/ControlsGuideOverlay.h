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
class Sprite;
class SpriteCommon;

// Tabで開く操作ガイド。能力図鑑を縦に読み、左右で基本操作へ切り替えます。
class ControlsGuideOverlay : public IEditable {
public:
    ~ControlsGuideOverlay() override;
    void DrawImGui() override;
    std::string GetName() override { return "能力ガイド (Ability Guide)"; }
    void Initialize(
        SpriteCommon* spriteCommon,
        Player* player,
        const std::string& layoutPath = "Resources/json/sprite/controlsGuide.json");
    void Finalize();

    void SetActive(bool active);
    bool IsActive() const { return isActive_; }
    void SetPlayer(Player* player) { player_ = player; }

    void Update(float deltaTime);
    void Draw();

    void CollectReplaySprites(std::vector<Sprite*>& sprites) const;
    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);

private:
    static constexpr int kPageCount = 2;
    static constexpr int kActionCount = 3;

    struct Element {
        Sprite* sprite = nullptr;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 baseSize = { 0.0f, 0.0f };
        Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct PageVisual {
        Element panel;
        Element portrait;
        std::array<Element, kActionCount> actionGlyphs{};
        std::array<Element, kActionCount> actionLabels{};
        std::vector<Element> details;
    };

    struct BookElement {
        Element visual;
        std::string slot;
        bool scroll = false;
        int row = -1;
        Vector2 textureSize = { 1.0f, 1.0f };
        Vector2 textureOrigin = { 0.0f, 0.0f };
    };

    void UpdateBook(float deltaTime);
    void LayoutBook(float offsetX);
    void RefreshBook();

    void LoadLayout(const std::string& layoutPath);
    void BindLayoutSprites();
    Sprite* FindSprite(const std::string& name) const;
    Element BindElement(const std::string& name) const;

    void SetAllVisible(bool visible);
    void SetPageVisible(int pageIndex, bool visible);
    void SetPageOffset(int pageIndex, float offsetX);
    void StartSlide(int direction);
    void UpdateSlide(float deltaTime);
    void UpdateStaticVisuals();
    void RefreshAbilityVisuals();
    void RefreshInputGlyphs(bool force = false);
    bool IsToggleHeld() const;

    SpriteCommon* spriteCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    Player* player_ = nullptr;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    Element background_;
    Element leftArrow_;
    Element rightArrow_;
    std::array<Element, kPageCount> pageDots_{};
    std::array<PageVisual, kPageCount> pages_{};

    bool isActive_ = false;
    bool inputGlyphGamepadMode_ = false;
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
    Vector2 layoutRatio_ = { 1.0f, 1.0f };
    std::string bookPreviewKey_;
    std::string editorStatus_;
};

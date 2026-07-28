#pragma once

#include "engine/utility/math/Math.h"
#include "json.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

class InputManager;
class Player;
class Sprite;
class SpriteCommon;

// Tabで開く操作ガイド。基本操作と現在能力のページを横スライドで切り替えます。
class ControlsGuideOverlay {
public:
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
    };

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
    void RefreshAbilityPortrait();
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
    bool suppressToggleInput_ = false;
    bool isSliding_ = false;
    int currentPage_ = 0;
    int outgoingPage_ = 0;
    int incomingPage_ = 0;
    int slideDirection_ = 1;
    float slideTimer_ = 0.0f;
    float slideDuration_ = 0.34f;
    float time_ = 0.0f;
    std::string portraitTexturePath_;
};

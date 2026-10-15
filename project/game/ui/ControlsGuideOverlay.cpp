#define NOMINMAX
#include "ControlsGuideOverlay.h"

#include "InputManager.h"
#include "game/ui/InputGlyphResolver.h"
#include "Player.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteLayoutScaler.h"
#include "SpriteLayoutFactory.h"
#include "TextureManager.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#ifdef USE_IMGUI
#include "imgui.h"
#include "EditorCommandRegistry.h"
#include "EditorManager.h"
#endif

using json = nlohmann::json;

namespace {
// ページ移動の補間率を0～1へ収め、両端の速度を滑らかにします。
float SmoothStep(float value) {
    // ページ移動の端で速度が急変しないよう、補間率を滑らかにする。
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

// 能力ガイドの三行分の説明画像。Sprite の生成や所有は呼出側で行う。
struct AbilityGuideVisual {
    std::array<const char*, 3> labelTextures{};
};

const char* ResolveBookKey(const Player* player) {
    // コピー状態を図鑑データの検索キーへ変換し、未変身は通常ページを使う。
    if (!player || !player->IsEnemyMorphed())
        return "normal";
    switch (player->GetEnemyMorphType()) {
    case Player::EnemyMorphType::Slime:
        return "pink";
    case Player::EnemyMorphType::Bomber:
        return "bomb";
    case Player::EnemyMorphType::FireSlime:
        return "fire";
    case Player::EnemyMorphType::ThunderSlime:
        return "thunder";
    case Player::EnemyMorphType::WindSlime:
        return "wind";
    case Player::EnemyMorphType::Crown:
        return "crown";
    default:
        return "other";
    }
}

// 現在のコピー能力に対応する三行の操作説明画像を選びます。
AbilityGuideVisual ResolveAbilityGuide(const Player* player) {
    // 現在のコピー能力に対応する三つの操作説明画像を選ぶ。
    if (!player) {
        return {};
    }

    if (!player->IsEnemyMorphed()) {
        return {{"ui/control_guide/labels/absorb.png", "ui/control_guide/labels/slime_attack.png",
                 "ui/control_guide/labels/hook_aim.png"}};
    }

    switch (player->GetEnemyMorphType()) {
    case Player::EnemyMorphType::Slime:
        return {{"ui/control_guide/labels/slime_dive.png", "ui/control_guide/labels/puni_straight.png",
                 "ui/control_guide/labels/puni_guard.png"}};
    case Player::EnemyMorphType::Bomber:
        return {{"ui/control_guide/labels/bomb_throw.png", "ui/control_guide/labels/bomb_place.png",
                 "ui/control_guide/labels/blast_jump.png"}};
    case Player::EnemyMorphType::FireSlime:
        return {{"ui/control_guide/labels/fireball.png", "ui/control_guide/labels/flame_breath.png",
                 "ui/control_guide/labels/blaze_step.png"}};
    case Player::EnemyMorphType::ThunderSlime:
        return {{"ui/control_guide/labels/thunder_chain.png", "ui/control_guide/labels/charged_discharge.png",
                 "ui/control_guide/labels/thunder_step.png"}};
    case Player::EnemyMorphType::WindSlime:
        return {{"ui/control_guide/labels/updraft.png", "ui/control_guide/labels/wind_breath.png",
                 "ui/control_guide/labels/wind_dash.png"}};
    default:
        return {};
    }
}

const char* ResolvePortraitTexture(const Player* player) {
    // 能力ごとの肖像画像を選び、未変身はプレイヤーの画像へ戻す。
    if (!player || !player->IsEnemyMorphed()) {
        return "ui/portraits/player.png";
    }

    switch (player->GetEnemyMorphType()) {
    case Player::EnemyMorphType::Slime:
        return "ui/control_guide/portraits/pink_slime.png";
    case Player::EnemyMorphType::Bomber:
        return "ui/control_guide/portraits/bomb_slime.png";
    case Player::EnemyMorphType::FireSlime:
        return "ui/control_guide/portraits/fire_slime.png";
    case Player::EnemyMorphType::ThunderSlime:
        return "ui/control_guide/portraits/thunder_slime.png";
    case Player::EnemyMorphType::WindSlime:
        return "ui/control_guide/portraits/wind_slime.png";
    case Player::EnemyMorphType::None:
    default:
        return "ui/portraits/player.png";
    }
}

void SetFullTextureRect(engine::Sprite* sprite, uint32_t textureHandle) {
    // 差し替え画像の元サイズをUV範囲へ反映し、旧画像の切り抜きが残らないようにする。
    if (!sprite || textureHandle == 0) {
        return;
    }

    sprite->SetTextureHandle(textureHandle);
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle);
    sprite->SetTextureRect({0.0f, 0.0f}, {static_cast<float>(metadata.width), static_cast<float>(metadata.height)});
}

engine::Vector2 SetGlyphTexture(engine::Sprite* sprite, const InputGlyphSpec& glyph) {
    // アトラス画像は全体の大きさではなく、該当キーの領域だけをSpriteへ渡す。
    const uint32_t handle = engine::Sprite::LoadTexture(glyph.path);
    SetFullTextureRect(sprite, handle);
    sprite->SetTextureName(glyph.path);
    if (glyph.useCrop) {
        sprite->SetTextureRect({glyph.cropX, glyph.cropY}, {glyph.width, glyph.height});
        return {glyph.width, glyph.height};
    }
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
    return {static_cast<float>(metadata.width), static_cast<float>(metadata.height)};
}
} // namespace

// レイアウト生成後に能力内容と入力表示を反映し、初期状態では全ページを隠します。
void ControlsGuideOverlay::Initialize(engine::SpriteCommon* spriteCommon, Player* player, const std::string& layoutPath) {
    // レイアウト読込後に現在能力と入力機器を反映し、表示は閉じた状態から始める。
    spriteCommon_ = spriteCommon;
    inputManager_ = InputManager::GetInstance();
    player_ = player;
    LoadLayout(layoutPath);
    BindLayoutSprites();
    RefreshAbilityVisuals();
    RefreshInputGlyphs(true);
    SetAllVisible(false);
#ifdef USE_IMGUI
    EditorCommand command;
    command.id = "ui.abilityGuide";
    command.displayName = "能力ガイドの調整";
    command.category = "UI";
    command.description = "実モデルの能力図鑑を表示して配置とスクロールを調整";
    command.keywords = {"Tab", "能力", "ガイド", "UI"};
    command.owner = this;
    command.canExecute = [this]() { return bookEnabled_; };
    command.execute = [this]() {
        currentPage_ = 1;
        SetActive(true);
        EditorManager::GetInstance()->SetSelectedObject(this);
    };
    EditorCommandRegistry::GetInstance()->Register(std::move(command));
#endif
}

ControlsGuideOverlay::~ControlsGuideOverlay() {
    // 編集コマンドと所有スプライトを破棄する共通経路を使う。
    Finalize();
}

void ControlsGuideOverlay::Finalize() {
    // エディターの選択参照を先に外し、所有する表示要素とプレイヤー参照を解放する。
#ifdef USE_IMGUI
    EditorCommandRegistry::GetInstance()->UnregisterOwner(this);
    if (EditorManager::GetInstance()->GetSelectedObject() == this) {
        EditorManager::GetInstance()->ClearSelection();
    }
#endif
    bookElements_.clear();
    bookData_ = {};
    bookKey_.clear();
    bookEnabled_ = false;
    bookVisible_ = false;
    sprites_.clear();
    background_ = {};
    leftArrow_ = {};
    rightArrow_ = {};
    pageDots_ = {};
    pages_ = {};
    spriteCommon_ = nullptr;
    inputManager_ = nullptr;
    player_ = nullptr;
    isActive_ = false;
    pauseMode_ = false;
    menuPageRequested_ = false;
    isSliding_ = false;
    portraitTexturePath_.clear();
    abilityLabelTexturePaths_ = {};
    abilityActionAvailable_ = {};
    inputGlyphGamepadMode_ = false;
    inputGlyphBindingSignature_.clear();
}

// 開いた直後のTab入力で閉じないよう、キーが離れるまで再トグルを抑制します。
void ControlsGuideOverlay::SetActive(bool active) {
    // 開閉の切替時にスライドを止め、開いた同じ入力で閉じないよう抑制する。
    if (isActive_ == active) {
        return;
    }

    isActive_ = active;
    menuPageRequested_ = false;
    if (!active)
        bookPreviewKey_.clear();
    suppressToggleInput_ = active;
    isSliding_ = false;
    slideTimer_ = 0.0f;
    outgoingPage_ = currentPage_;
    incomingPage_ = currentPage_;
    RefreshAbilityVisuals();
    if (active)
        RefreshInputGlyphs(true);
    SetAllVisible(active);
    if (active) {
        SetPageVisible(currentPage_, true);
        SetPageOffset(currentPage_, 0.0f);
        UpdateStaticVisuals();
    }
}

void ControlsGuideOverlay::SetPauseMode(bool pauseMode) {
    // 独立ガイドとポーズ内の能力ページで、閉じ方を示すフッターだけ切り替える。
    if (pauseMode_ == pauseMode) {
        return;
    }
    pauseMode_ = pauseMode;
    RefreshInputGlyphs(true);
}

void ControlsGuideOverlay::OpenAbilityPage(bool resetScroll) {
    // 前回見ていた基本操作ページが残っていても、ポーズからは現在能力を直接確認する。
    currentPage_ = 1;
    isSliding_ = false;
    if (resetScroll) {
        bookTargetScroll_ = bookScroll_ = 0.0f;
    }
    SetActive(true);
}

bool ControlsGuideOverlay::ConsumeMenuPageRequest() {
    const bool requested = menuPageRequested_;
    menuPageRequested_ = false;
    return requested;
}

// ページ入力より先に能力と入力機器の変化を反映します。閉じても選択ページは保持します。
void ControlsGuideOverlay::Update(float deltaTime, bool acceptInput) {
    if (!isActive_) {
        return;
    }

    time_ += deltaTime;
    RefreshAbilityVisuals();
    RefreshInputGlyphs();

    // 開く操作を同一フレームで閉じる操作として扱わないための判定です。
    if (suppressToggleInput_) {
        if (!IsToggleHeld()) {
            suppressToggleInput_ = false;
        }
    } else if (acceptInput && inputManager_
#ifdef USE_IMGUI
               && (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantTextInput)
#endif
    ) {
        if (inputManager_->IsKeyTriggered(DIK_TAB) ||
            inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B) ||
            inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_BACK)) {
            SetActive(false);
            return;
        }

        if (!isSliding_) {
            const bool left = inputManager_->IsKeyTriggered(DIK_LEFT) || inputManager_->IsKeyTriggered(DIK_A) ||
                              inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT) || inputManager_->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_LEFT);
            const bool right = inputManager_->IsKeyTriggered(DIK_RIGHT) || inputManager_->IsKeyTriggered(DIK_D) ||
                               inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT) || inputManager_->IsMenuStickTriggered(XINPUT_GAMEPAD_DPAD_RIGHT);

            const bool confirm = inputManager_->IsKeyTriggered(DIK_SPACE) ||
                                 inputManager_->IsKeyTriggered(DIK_RETURN) ||
                                 inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A);
            if (confirm) {
                // 基本操作は能力ページへ進み、能力ページからはポーズメニューへ進む。
                if (pauseMode_ && currentPage_ == 1) menuPageRequested_ = true;
                else StartSlide(1);
            } else if (left != right) {
                // ポーズでは能力→メニューが連続した次ページ。基本操作へは左で戻れる。
                if (pauseMode_ && currentPage_ == 1 && right) {
                    menuPageRequested_ = true;
                } else if (!(pauseMode_ && currentPage_ == 0 && left)) {
                    StartSlide(left ? -1 : 1);
                }
            }
        }
    }

    UpdateBook(deltaTime);
    UpdateSlide(deltaTime);
    UpdateStaticVisuals();

    for (auto& sprite : sprites_) {
        if (sprite && sprite->IsVisible()) {
            sprite->Update();
        }
    }
}

void ControlsGuideOverlay::SetMenuTransitionOffset(float offsetX, bool transitioning) {
    // 移動中の矢印とページ点は重複表示させず、背景だけを固定する。
    SetPageOffset(currentPage_, offsetX);
    for (Element* element : {&leftArrow_, &rightArrow_, &pageDots_[0], &pageDots_[1]})
        if (element->sprite) element->sprite->SetVisible(isActive_ && !transitioning);
    for (auto& sprite : sprites_)
        if (sprite && sprite->IsVisible()) sprite->Update();
}

void ControlsGuideOverlay::Draw() {
    // ガイドが閉じている間は所有スプライトの描画を全て省く。
    if (!isActive_) {
        return;
    }

    for (auto& sprite : sprites_) {
        if (sprite && sprite->IsVisible()) {
            sprite->Draw();
        }
    }
}

void ControlsGuideOverlay::CollectReplaySprites(std::vector<engine::Sprite*>& replaySprites) const {
    // リプレイへは所有権を移さず、既存スプライトの参照だけ渡す。
    replaySprites.reserve(replaySprites.size() + sprites_.size());
    for (const auto& sprite : sprites_) {
        if (sprite) {
            replaySprites.push_back(sprite.get());
        }
    }
}

void ControlsGuideOverlay::CaptureReplayState(json& state) const {
    // 開閉、ページ移動、図鑑スクロールを同じフレームへ戻せるよう記録する。
    state = {{"active", isActive_},
             {"pauseMode", pauseMode_},
             {"suppressInput", suppressToggleInput_},
             {"sliding", isSliding_},
             {"currentPage", currentPage_},
             {"outgoingPage", outgoingPage_},
             {"incomingPage", incomingPage_},
             {"slideDirection", slideDirection_},
             {"slideTimer", slideTimer_},
             {"time", time_}};
    state["bookScroll"] = bookScroll_;
    state["bookTargetScroll"] = bookTargetScroll_;
}

// 入力値を範囲内へ丸めた後に表示を組み直し、再生位置と見た目を一致させます。
void ControlsGuideOverlay::RestoreReplayState(const json& state) {
    // 記録値をページ・スクロール範囲へ収め、再生位置に合わせて表示を組み直す。
    if (!state.is_object()) {
        return;
    }

    isActive_ = state.value("active", isActive_);
    pauseMode_ = state.value("pauseMode", pauseMode_);
    suppressToggleInput_ = state.value("suppressInput", suppressToggleInput_);
    isSliding_ = state.value("sliding", isSliding_);
    currentPage_ = std::clamp(state.value("currentPage", currentPage_), 0, kPageCount - 1);
    outgoingPage_ = std::clamp(state.value("outgoingPage", outgoingPage_), 0, kPageCount - 1);
    incomingPage_ = std::clamp(state.value("incomingPage", incomingPage_), 0, kPageCount - 1);
    slideDirection_ = state.value("slideDirection", slideDirection_) < 0 ? -1 : 1;
    slideTimer_ = std::max(0.0f, state.value("slideTimer", slideTimer_));
    time_ = state.value("time", time_);

    RefreshAbilityVisuals();
    RefreshInputGlyphs(true);
    bookScroll_ = std::clamp(state.value("bookScroll", 0.0f), 0.0f, bookMaxScroll_);
    bookTargetScroll_ = std::clamp(state.value("bookTargetScroll", bookScroll_), 0.0f, bookMaxScroll_);
    SetAllVisible(isActive_);
    if (isActive_) {
        UpdateSlide(0.0f);
        UpdateStaticVisuals();
    }
}

// JSONのスプライト配置と能力図鑑データを読み、所有する表示要素を作り直します。
void ControlsGuideOverlay::LoadLayout(const std::string& layoutPath) {
    layoutPath_ = layoutPath;
    bookElements_.clear();
    bookEnabled_ = false;
    bookData_ = {};
    bookKey_.clear();
    sprites_.clear();
    if (!spriteCommon_) {
        return;
    }

    std::ifstream file(layoutPath);
    if (!file.is_open()) {
        return;
    }

    json root;
    try {
        file >> root;
    } catch (...) {
        return;
    }

    if (!root.contains("sprites") || !root["sprites"].is_array()) {
        return;
    }

    const auto layoutScale = SpriteLayoutScaler::Make(root);
    layoutRatio_ = layoutScale.enabled ? layoutScale.ratio : engine::Vector2{1.0f, 1.0f};
    if (root.contains("abilityBook") && root["abilityBook"].is_object()) {
        const auto& book = root["abilityBook"];
        const float scaleY = layoutScale.enabled ? layoutScale.ratio.y : 1.0f;
        bookEnabled_ = SpriteLayoutScaler::ReadBool(book, "enabled", true);
        bookTop_ = SpriteLayoutScaler::ReadFloat(book, "viewportTop", 492.0f) * scaleY;
        bookBottom_ = std::max(bookTop_ + 1.0f,
                               SpriteLayoutScaler::ReadFloat(book, "viewportBottom", 878.0f) * scaleY);
        bookMaxScroll_ = std::max(0.0f,
                                  SpriteLayoutScaler::ReadFloat(book, "contentBottom", 1520.0f) * scaleY - bookBottom_);
        bookScrollSpeed_ = std::max(1.0f, SpriteLayoutScaler::ReadFloat(book, "scrollSpeed", 650.0f)) * scaleY;
        bookWheelStep_ = std::max(1.0f, SpriteLayoutScaler::ReadFloat(book, "wheelStep", 125.0f)) * scaleY;
        bookBob_ = std::clamp(SpriteLayoutScaler::ReadFloat(book, "portraitBob", 4.0f), 0.0f, 12.0f) * scaleY;
        try {
            std::ifstream dataFile(SpriteLayoutScaler::ReadString(book, "dataPath",
                                                               "Resources/json/ui/abilityGuideBook.json"));
            if (dataFile)
                dataFile >> bookData_;
        } catch (...) {
            bookData_ = {};
        }
    }
    for (const auto& spriteData : root["sprites"]) {
        if (!spriteData.is_object()) {
            continue;
        }
        // 任意の切り抜き領域は4つの有限な数値がそろった場合だけ適用します。
        bool hasTextureCrop = false;
        engine::Vector2 textureOrigin{};
        engine::Vector2 textureSize{};
        if (spriteData.contains("textureCrop")) {
            const auto& crop = spriteData["textureCrop"];
            if (crop.is_array() && crop.size() >= 4 && crop[0].is_number() && crop[1].is_number() &&
                crop[2].is_number() && crop[3].is_number()) {
                textureOrigin = {crop[0].get<float>(), crop[1].get<float>()};
                textureSize = {crop[2].get<float>(), crop[3].get<float>()};
                hasTextureCrop = std::isfinite(textureOrigin.x) && std::isfinite(textureOrigin.y) &&
                                 std::isfinite(textureSize.x) && std::isfinite(textureSize.y);
            }
        }

        auto sprite = SpriteLayoutFactory::Create(spriteCommon_, spriteData, layoutScale, "controls_guide_sprite");
        const std::string name = sprite->GetName();
        const uint32_t textureHandle = sprite->GetTextureHandle();
        if (hasTextureCrop) {
            sprite->SetTextureRect(textureOrigin, textureSize);
        }
        sprite->Update();
        sprites_.push_back(std::move(sprite));
        if (bookEnabled_ && SpriteLayoutScaler::ReadBool(spriteData, "bookElement", false)) {
            BookElement element;
            element.visual = BindElement(name);
            element.slot = SpriteLayoutScaler::ReadString(spriteData, "bookSlot", "");
            element.scroll = SpriteLayoutScaler::ReadBool(spriteData, "bookScroll", false);
            element.row = SpriteLayoutScaler::ReadInt(spriteData, "bookRow", -1);
            const auto& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle);
            element.textureSize = {static_cast<float>(metadata.width), static_cast<float>(metadata.height)};
            if (hasTextureCrop) {
                element.textureOrigin = textureOrigin;
                element.textureSize = textureSize;
            }
            bookElements_.push_back(std::move(element));
        }
    }
}

// 名前で生成済みスプライトを拾い、ページごとの描画要素へ割り当てます。
void ControlsGuideOverlay::BindLayoutSprites() {
    // JSONで生成した名前付きSpriteをページ・矢印・図鑑要素へ割り当てる。
    background_ = BindElement("controls_guide_background");
    leftArrow_ = BindElement("controls_guide_left_arrow");
    rightArrow_ = BindElement("controls_guide_right_arrow");

    for (int page = 0; page < kPageCount; ++page) {
        const std::string prefix = "controls_guide_page_" + std::to_string(page);
        pages_[static_cast<size_t>(page)].panel = BindElement(prefix + "_panel");
        pages_[static_cast<size_t>(page)].portrait = BindElement(prefix + "_portrait");
        auto& details = pages_[static_cast<size_t>(page)].details;
        details.clear();
        for (const auto& sprite : sprites_) {
            if (sprite && sprite->GetName().rfind(prefix + "_detail_", 0) == 0) {
                details.push_back(BindElement(sprite->GetName()));
            }
        }
        for (int action = 0; action < kActionCount; ++action) {
            pages_[static_cast<size_t>(page)].actionGlyphs[static_cast<size_t>(action)] =
                BindElement(prefix + "_action_" + std::to_string(action));
            pages_[static_cast<size_t>(page)].actionLabels[static_cast<size_t>(action)] =
                BindElement(prefix + "_action_label_" + std::to_string(action));
        }
        pageDots_[static_cast<size_t>(page)] = BindElement("controls_guide_page_dot_" + std::to_string(page));
    }
}

engine::Sprite* ControlsGuideOverlay::FindSprite(const std::string& name) const {
    // レイアウト名が一致する所有Spriteを探し、未定義ならnullptrを返す。
    for (const auto& sprite : sprites_) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr;
}

ControlsGuideOverlay::Element ControlsGuideOverlay::BindElement(const std::string& name) const {
    // 名前で見つけたSpriteと配置基準を一つの表示要素として保持する。
    Element element;
    element.sprite = FindSprite(name);
    if (element.sprite) {
        element.basePosition = element.sprite->GetPosition();
        element.baseSize = element.sprite->GetSize();
        element.baseColor = element.sprite->GetColor();
    }
    return element;
}

void ControlsGuideOverlay::SetAllVisible(bool visible) {
    // ページ切替前に全要素の可視性を揃え、前ページの残像を防ぐ。
    bookVisible_ = visible && currentPage_ == 1;
    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->SetVisible(visible);
        }
    }

    if (!visible) {
        return;
    }

    for (int page = 0; page < kPageCount; ++page) {
        SetPageVisible(page, page == currentPage_);
    }
}

void ControlsGuideOverlay::SetPageVisible(int pageIndex, bool visible) {
    // 指定ページに属する要素だけを切り替え、他ページの状態を保持する。
    if (pageIndex < 0 || pageIndex >= kPageCount) {
        return;
    }

    PageVisual& page = pages_[static_cast<size_t>(pageIndex)];
    if (pageIndex == 1 && bookEnabled_) {
        bookVisible_ = visible;
        LayoutBook(bookOffsetX_);
    }
    if (page.panel.sprite)
        page.panel.sprite->SetVisible(visible);
    if (page.portrait.sprite)
        page.portrait.sprite->SetVisible(visible);
    for (Element& detail : page.details) {
        if (detail.sprite)
            detail.sprite->SetVisible(visible);
    }
    for (int action = 0; action < kActionCount; ++action) {
        const bool actionVisible = visible && (pageIndex != 1 || abilityActionAvailable_[static_cast<size_t>(action)]);
        Element& glyph = page.actionGlyphs[static_cast<size_t>(action)];
        Element& label = page.actionLabels[static_cast<size_t>(action)];
        if (glyph.sprite)
            glyph.sprite->SetVisible(actionVisible);
        if (label.sprite)
            label.sprite->SetVisible(actionVisible);
    }

}

void ControlsGuideOverlay::SetPageOffset(int pageIndex, float offsetX) {
    // ページ移動量を基準配置へ加え、スライド後に位置を戻せるようにする。
    if (pageIndex < 0 || pageIndex >= kPageCount) {
        return;
    }

    PageVisual& page = pages_[static_cast<size_t>(pageIndex)];
    const auto applyOffset = [offsetX](Element& element) {
        if (element.sprite) {
            element.sprite->SetPosition({element.basePosition.x + offsetX, element.basePosition.y});
        }
    };

    applyOffset(page.panel);
    applyOffset(page.portrait);
    for (Element& detail : page.details)
        applyOffset(detail);
    for (size_t index = 0; index < page.actionGlyphs.size(); ++index) {
        Element& glyph = page.actionGlyphs[index];
        applyOffset(glyph);
        const engine::Vector2 textureSize = basicGlyphTextureSizes_[index];
        if (pageIndex == 0 && glyph.sprite && textureSize.x > 0.0f && textureSize.y > 0.0f) {
            // 縮小によって生じる余白を両側へ配分する。左上寄せではSpaceやパッドだけ中心がずれる。
            const float fit = (std::min)(glyph.baseSize.x / textureSize.x, glyph.baseSize.y / textureSize.y);
            const engine::Vector2 size = {textureSize.x * fit, textureSize.y * fit};
            const engine::Vector2 anchor = glyph.sprite->GetAnchorPoint();
            glyph.sprite->SetSize(size);
            glyph.sprite->SetPosition({
                glyph.basePosition.x + offsetX + (glyph.baseSize.x - size.x) * (0.5f - anchor.x),
                glyph.basePosition.y + (glyph.baseSize.y - size.y) * (0.5f - anchor.y)});
        }
    }
    for (Element& label : page.actionLabels) {
        applyOffset(label);
    }
    if (pageIndex == 1 && bookEnabled_)
        LayoutBook(offsetX);
}

// 送り元と送り先を両方表示し、移動完了時に送り元だけ隠します。
void ControlsGuideOverlay::StartSlide(int direction) {
    // 現ページと隣ページを同時に見せ、方向に応じたスライドを始める。
    if (isSliding_ || direction == 0) {
        return;
    }

    slideDirection_ = direction < 0 ? -1 : 1;
    outgoingPage_ = currentPage_;
    incomingPage_ = (currentPage_ + slideDirection_ + kPageCount) % kPageCount;
    slideTimer_ = 0.0f;
    isSliding_ = true;
    SetPageVisible(outgoingPage_, true);
    SetPageVisible(incomingPage_, true);
    UpdateSlide(0.0f);
}

void ControlsGuideOverlay::UpdateSlide(float deltaTime) {
    // 補間率で二枚を反対方向へ動かし、完了時に送り元を隠す。
    if (!isSliding_) {
        SetPageOffset(currentPage_, 0.0f);
        return;
    }

    slideTimer_ += std::max(0.0f, deltaTime);
    const float rate = slideDuration_ > 0.0f ? std::clamp(slideTimer_ / slideDuration_, 0.0f, 1.0f) : 1.0f;
    const float eased = SmoothStep(rate);
    const float slideDistance = background_.baseSize.x > 0.0f ? background_.baseSize.x * 0.86f : 1650.0f;

    SetPageOffset(outgoingPage_, -static_cast<float>(slideDirection_) * slideDistance * eased);
    SetPageOffset(incomingPage_, static_cast<float>(slideDirection_) * slideDistance * (1.0f - eased));

    if (rate >= 1.0f) {
        SetPageVisible(outgoingPage_, false);
        currentPage_ = incomingPage_;
        SetPageOffset(currentPage_, 0.0f);
        isSliding_ = false;
        slideTimer_ = 0.0f;
    }
}

void ControlsGuideOverlay::UpdateStaticVisuals() {
    // 選択ページに応じた矢印やページ位置の表示を更新する。
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 5.2f);

    const auto updateArrow = [pulse](Element& arrow, float phase) {
        if (!arrow.sprite) {
            return;
        }
        const float scale = 1.0f + pulse * 0.08f;
        arrow.sprite->SetSize({arrow.baseSize.x * scale, arrow.baseSize.y * scale});
        arrow.sprite->SetPosition({arrow.basePosition.x + std::sin(phase) * 3.0f, arrow.basePosition.y});
        arrow.sprite->SetColor({1.0f, 0.96f, 0.62f + pulse * 0.18f, 1.0f});
    };

    updateArrow(leftArrow_, time_ * 4.4f);
    updateArrow(rightArrow_, time_ * 4.4f + 3.1415926535f);

    for (int page = 0; page < kPageCount; ++page) {
        Element& dot = pageDots_[static_cast<size_t>(page)];
        if (!dot.sprite) {
            continue;
        }
        const bool selected = page == currentPage_ || (isSliding_ && page == incomingPage_);
        const float scale = selected ? 1.16f : 0.82f;
        dot.sprite->SetSize({dot.baseSize.x * scale, dot.baseSize.y * scale});
        dot.sprite->SetColor(selected ? engine::Vector4{1.0f, 0.86f, 0.28f, 1.0f} : engine::Vector4{0.65f, 0.90f, 1.0f, 0.72f});
    }
}

// コピー能力が変わった時だけ画像を差し替え、毎フレームの再読込を避けます。
void ControlsGuideOverlay::RefreshAbilityVisuals() {
    // 能力変更時だけ説明画像と肖像を差し替え、不要な毎フレーム読込を避ける。
    if (bookEnabled_) {
        RefreshBook();
        return;
    }
    const std::string nextPath = ResolvePortraitTexture(player_);
    if (nextPath != portraitTexturePath_) {
        portraitTexturePath_ = nextPath;
        engine::Sprite* portrait = pages_[1].portrait.sprite;
        if (portrait) {
            const uint32_t textureHandle = engine::Sprite::LoadTexture(nextPath);
            SetFullTextureRect(portrait, textureHandle);
            portrait->SetTextureName(nextPath);
        }
    }

    const AbilityGuideVisual guide = ResolveAbilityGuide(player_);
    bool guideChanged = false;
    for (int action = 0; action < kActionCount; ++action) {
        const char* texturePath = guide.labelTextures[static_cast<size_t>(action)];
        const std::string nextLabelPath = texturePath ? texturePath : "";
        const bool available = !nextLabelPath.empty();

        guideChanged |= abilityActionAvailable_[static_cast<size_t>(action)] != available;
        abilityActionAvailable_[static_cast<size_t>(action)] = available;

        if (abilityLabelTexturePaths_[static_cast<size_t>(action)] == nextLabelPath) {
            continue;
        }

        guideChanged = true;
        abilityLabelTexturePaths_[static_cast<size_t>(action)] = nextLabelPath;
        engine::Sprite* label = pages_[1].actionLabels[static_cast<size_t>(action)].sprite;
        if (label && available) {
            const uint32_t textureHandle = engine::Sprite::LoadTexture(nextLabelPath);
            SetFullTextureRect(label, textureHandle);
            label->SetTextureName(nextLabelPath);
        }
    }

    if (!guideChanged || !isActive_) {
        return;
    }

    const bool abilityPageVisible = isSliding_ ? (outgoingPage_ == 1 || incomingPage_ == 1) : currentPage_ == 1;
    SetPageVisible(1, abilityPageVisible);
}

// 入力機器が切り替わった時にキー画像を更新し、元の枠へ収まる大きさへ調整します。
void ControlsGuideOverlay::RefreshInputGlyphs(bool force) {
    // 入力機器が変わったときだけキー表示を差し替え、同時表示による混乱を防ぐ。
    const bool gamepadMode = inputManager_ && inputManager_->IsGamepadMode();
    std::string signature = GetInputGlyphBindingSignature();
    if (!force && inputGlyphGamepadMode_ == gamepadMode && inputGlyphBindingSignature_ == signature)
        return;
    inputGlyphGamepadMode_ = gamepadMode;
    inputGlyphBindingSignature_ = std::move(signature);

    // ページ共通の操作案内を差し替え、キーボードとパッドの表記を同時に出さない。
    const std::string footerSuffix = gamepadMode ? "gamepad.png" : "keyboard.png";
    for (const char* action : {"close", "confirm"}) {
        engine::Sprite* footer = FindSprite(std::string("controls_guide_footer_") + action + "_text");
        if (!footer) continue;
        const std::string path = "generated/text/pause_guide/footer_" + std::string(action) + "_" + footerSuffix;
        if (footer->GetTextureName() != path) {
            SetFullTextureRect(footer, engine::Sprite::LoadTexture(path));
            footer->SetTextureName(path);
        }
    }

    // 操作画像だけを切り替えると説明文に別の入力機器が残るため、短い案内も同時に更新する。
    for (int action = 0; action < kActionCount; ++action) {
        if (engine::Sprite* condition = FindSprite("controls_guide_page_0_detail_condition_" + std::to_string(action))) {
            const std::string path = "ui/control_guide/book/basics/move_" + std::to_string(action) +
                                     (gamepadMode ? "_condition_gamepad.png" : "_condition.png");
            if (condition->GetTextureName() != path) {
                SetFullTextureRect(condition, engine::Sprite::LoadTexture(path));
                condition->SetTextureName(path);
            }
        }
    }
    if (engine::Sprite* footer = FindSprite("controls_guide_page_0_detail_footer")) {
        const std::string path = pauseMode_
                                     ? (gamepadMode ? "generated/text/pause_guide/basic_footer_gamepad.png"
                                                    : "generated/text/pause_guide/basic_footer_keyboard.png")
                                     : (gamepadMode ? "ui/control_guide/book/basics/footer_gamepad.png"
                                                    : "ui/control_guide/book/basics/footer.png");
        if (footer->GetTextureName() != path) {
            SetFullTextureRect(footer, engine::Sprite::LoadTexture(path));
            footer->SetTextureName(path);
        }
    }

    constexpr std::array<const char*, kActionCount> basicGlyphs = {
        "ui/input/combo/combo_wasd.png", "ui/input/keyboard/key_space.png", "ui/input/mouse/mouse_move.png"};
    for (int action = 0; action < kActionCount; ++action) {
        Element& visual = pages_[0].actionGlyphs[static_cast<size_t>(action)];
        if (!visual.sprite)
            continue;
        basicGlyphTextureSizes_[static_cast<size_t>(action)] = SetGlyphTexture(
            visual.sprite, ResolveInputGlyph(basicGlyphs[static_cast<size_t>(action)], gamepadMode));
    }
    const json fallback = json::object();
    const json& entry = bookData_.contains(bookKey_) ? bookData_[bookKey_] : fallback;
    for (BookElement& item : bookElements_) {
        if (item.visual.sprite && item.visual.sprite->GetName() == "controls_guide_page_1_book_footer") {
            const std::string path = pauseMode_
                                         ? (gamepadMode ? "generated/text/pause_guide/page_footer_gamepad.png"
                                                        : "generated/text/pause_guide/page_footer_keyboard.png")
                                         : (gamepadMode ? "ui/control_guide/book/footer_gamepad.png"
                                                        : "ui/control_guide/book/footer.png");
            if (item.visual.sprite->GetTextureName() != path) {
                const uint32_t handle = engine::Sprite::LoadTexture(path);
                SetFullTextureRect(item.visual.sprite, handle);
                item.visual.sprite->SetTextureName(path);
                const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
                item.textureOrigin = {};
                item.textureSize = {static_cast<float>(metadata.width), static_cast<float>(metadata.height)};
            }
        }
        if (item.slot.rfind("glyph_", 0) != 0 || !item.visual.sprite)
            continue;
        const std::string keyboardPath =
            item.row >= 0 && item.row < bookMoveCount_
                ? entry["moves"][item.row].value("glyph", std::string("ui/input/keyboard/key_e.png"))
                : "ui/input/keyboard/key_e.png";
        const InputGlyphSpec glyph = ResolveInputGlyph(keyboardPath, gamepadMode);
        item.textureSize = SetGlyphTexture(item.visual.sprite, glyph);
        item.textureOrigin = glyph.useCrop ? engine::Vector2{glyph.cropX, glyph.cropY} : engine::Vector2{};
    }
    if (bookEnabled_)
        LayoutBook(bookOffsetX_);
}

// 能力種別が変わった時は図鑑の画像とスクロール位置を一緒に更新します。
void ControlsGuideOverlay::RefreshBook() {
    // コピー能力の図鑑データを選び直し、スクロール位置も新しい内容へ合わせる。
    const std::string nextKey = bookPreviewKey_.empty() ? ResolveBookKey(player_) : bookPreviewKey_;
    if (nextKey == bookKey_)
        return;
    bookKey_ = nextKey;
    bookScroll_ = bookTargetScroll_ = 0.0f;
    const json fallback = json::object();
    const json& entry = bookData_.contains(bookKey_) ? bookData_[bookKey_] : fallback;
    bookMoveCount_ = entry.contains("moves") && entry["moves"].is_array() ? static_cast<int>(entry["moves"].size()) : 0;
    for (BookElement& item : bookElements_) {
        if (item.slot.empty() || !item.visual.sprite)
            continue;
        std::string path = "ui/control_guide/book/" + bookKey_ + "/" + item.slot + ".png";
        if (item.slot.rfind("glyph_", 0) == 0) {
            path = item.row >= 0 && item.row < bookMoveCount_
                       ? entry["moves"][item.row].value("glyph", std::string("ui/input/keyboard/key_e.png"))
                       : "ui/input/keyboard/key_e.png";
            const InputGlyphSpec glyph = ResolveInputGlyph(path, inputManager_ && inputManager_->IsGamepadMode());
            item.textureSize = SetGlyphTexture(item.visual.sprite, glyph);
            item.textureOrigin = glyph.useCrop ? engine::Vector2{glyph.cropX, glyph.cropY} : engine::Vector2{};
            continue;
        }
        const uint32_t handle = engine::Sprite::LoadTexture(path);
        SetFullTextureRect(item.visual.sprite, handle);
        item.visual.sprite->SetTextureName(path);
        const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
        item.textureSize = {static_cast<float>(metadata.width), static_cast<float>(metadata.height)};
    }
    portraitTexturePath_ =
        nextKey == "other" ? ResolvePortraitTexture(player_) : "ui/control_guide/book/portraits/" + nextKey + ".png";
    if (engine::Sprite* portrait = pages_[1].portrait.sprite) {
        SetFullTextureRect(portrait, engine::Sprite::LoadTexture(portraitTexturePath_));
        portrait->SetTextureName(portraitTexturePath_);
    }
    LayoutBook(bookOffsetX_);
}

void ControlsGuideOverlay::UpdateBook(float deltaTime) {
    // 能力と技の説明は一画面に収める。古いリプレイのスクロール値も表示へ影響させない。
    if (!bookEnabled_)
        return;
    (void)deltaTime;
    bookScroll_ = bookTargetScroll_ = 0.0f;
    LayoutBook(bookOffsetX_);
}

// 図鑑内のSpriteをページ移動量に合わせ、キー画像は枠内へ縮小します。
// スクロール領域はUVも一緒に切り取り、見切れた文字が枠外へ出ないようにします。
void ControlsGuideOverlay::LayoutBook(float offsetX) {
    bookOffsetX_ = offsetX;
    for (BookElement& item : bookElements_) {
        Element& element = item.visual;
        engine::Sprite* sprite = element.sprite;
        if (!sprite)
            continue;
        const bool available = item.row < 0 || item.row < bookMoveCount_;
        sprite->SetVisible(bookVisible_ && available);
        engine::Vector2 position = {element.basePosition.x + offsetX, element.basePosition.y};
        engine::Vector2 size = element.baseSize;
        engine::Vector2 uv = item.textureOrigin;
        engine::Vector2 uvSize = item.textureSize;
        if (item.slot.rfind("glyph_", 0) == 0 && item.textureSize.x > 0.0f && item.textureSize.y > 0.0f) {
            const float fit =
                (std::min)(element.baseSize.x / item.textureSize.x, element.baseSize.y / item.textureSize.y);
            size = {item.textureSize.x * fit, item.textureSize.y * fit};
            position.x += (element.baseSize.x - size.x) * 0.5f;
            position.y += (element.baseSize.y - size.y) * 0.5f;
        }
        if (item.scroll) {
            const float y = element.basePosition.y - bookScroll_;
            const float top = std::max(y, bookTop_);
            const float bottom = std::min(y + size.y, bookBottom_);
            if (bottom <= top || size.y <= 0.0f) {
                sprite->SetVisible(false);
                continue;
            }
            // UVと描画サイズを同時に切り取り、巻物の上下へ文章をはみ出させない。
            uv.y += (top - y) / size.y * item.textureSize.y;
            uvSize.y = (bottom - top) / size.y * item.textureSize.y;
            position.y = top;
            size.y = bottom - top;
        }
        sprite->SetPosition(position);
        sprite->SetSize(size);
        sprite->SetTextureRect(uv, uvSize);
    }
    if (engine::Sprite* portrait = pages_[1].portrait.sprite) {
        const Element& element = pages_[1].portrait;
        // 立ち絵を小さく呼吸させ、説明を読む間も現在の能力が生きて見えるようにする。
        const float breath = 1.0f + std::sin(time_ * 2.4f) * 0.025f;
        const engine::Vector2 size{element.baseSize.x * breath, element.baseSize.y * breath};
        portrait->SetSize(size);
        portrait->SetPosition({element.basePosition.x + offsetX - (size.x - element.baseSize.x) * 0.5f,
                               element.basePosition.y + std::sin(time_ * 2.0f) * bookBob_ -
                                   (size.y - element.baseSize.y) * 0.5f});
    }
}

void ControlsGuideOverlay::DrawImGui() {
    // 実レイアウトを見ながら図鑑の配置とスクロールを調整する編集欄を出す。
#ifdef USE_IMGUI
    ImGui::TextWrapped("Tabの能力図鑑。表示確認だけでプレイヤーの能力は変更しません。");
    const char* keys[] = {"", "normal", "pink", "fire", "thunder", "wind", "bomb"};
    const char* names[] = {"現在のプレイヤー", "通常", "ピンク", "炎", "雷", "風", "ボム"};
    int selected = 0;
    for (int i = 0; i < 7; ++i)
        if (bookPreviewKey_ == keys[i])
            selected = i;
    if (ImGui::Combo("表示能力", &selected, names, 7)) {
        bookPreviewKey_ = keys[selected];
        RefreshBook();
    }
    if (ImGui::Button("ガイドを開く")) {
        currentPage_ = 1;
        isSliding_ = false;
        SetActive(true);
        SetAllVisible(true);
        SetPageOffset(1, 0.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("閉じる")) {
        bookPreviewKey_.clear();
        SetActive(false);
    }
    ImGui::DragFloat("肖像の上下揺れ", &bookBob_, 0.1f, 0.0f, 12.0f * layoutRatio_.y);
    const auto editElement = [](const char* label, Element& element) {
        if (!element.sprite || !ImGui::TreeNode(label))
            return;
        ImGui::DragFloat2("位置", &element.basePosition.x, 1.0f);
        ImGui::DragFloat2("サイズ", &element.baseSize.x, 1.0f, 1.0f, 4000.0f);
        element.sprite->SetSize(element.baseSize);
        ImGui::TreePop();
    };
    editElement("巻物パネル", pages_[1].panel);
    editElement("実モデル肖像", pages_[1].portrait);
    if (ImGui::Button("基本操作を表示")) {
        currentPage_ = 0;
        isSliding_ = false;
        SetActive(true);
        SetAllVisible(true);
        SetPageOffset(0, 0.0f);
    }
    if (ImGui::TreeNode("基本操作ページの配置")) {
        editElement("基本操作パネル", pages_[0].panel);
        editElement("通常モデル肖像", pages_[0].portrait);
        for (Element& detail : pages_[0].details)
            editElement(detail.sprite->GetName().c_str(), detail);
        for (Element& glyph : pages_[0].actionGlyphs) {
            if (glyph.sprite)
                editElement(glyph.sprite->GetName().c_str(), glyph);
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("能力ページの文字配置")) {
        for (BookElement& item : bookElements_) {
            editElement(item.visual.sprite->GetName().c_str(), item.visual);
        }
        ImGui::TreePop();
    }
    if (ImGui::Button("レイアウトJSONへ保存")) {
        try {
            json layout;
            {
                std::ifstream input(layoutPath_);
                input >> layout;
            }
            const auto saveElement = [this, &layout](const Element& element) {
                if (!element.sprite)
                    return;
                for (auto& data : layout["sprites"]) {
                    if (data.value("name", std::string{}) != element.sprite->GetName())
                        continue;
                    data["position"] = {element.basePosition.x / layoutRatio_.x,
                                        element.basePosition.y / layoutRatio_.y};
                    data["size"] = {element.baseSize.x / layoutRatio_.x, element.baseSize.y / layoutRatio_.y};
                }
            };
            saveElement(pages_[1].panel);
            saveElement(pages_[1].portrait);
            saveElement(pages_[0].panel);
            saveElement(pages_[0].portrait);
            for (const Element& detail : pages_[0].details)
                saveElement(detail);
            for (const Element& glyph : pages_[0].actionGlyphs)
                saveElement(glyph);
            for (const BookElement& item : bookElements_)
                saveElement(item.visual);
            layout["abilityBook"]["portraitBob"] = bookBob_ / layoutRatio_.y;
            std::ofstream output(layoutPath_);
            output << layout.dump(2) << '\n';
            output.flush();
            editorStatus_ = output.good() ? "レイアウトを保存しました。" : "書き込みに失敗しました。";
        } catch (...) {
            editorStatus_ = "保存に失敗しました。JSONを確認してください。";
        }
    }
    ImGui::TextWrapped("文章は Resources/json/ui/abilityGuideBook.json で編集できます。変更後は "
                       "generate_ability_book.ps1 で文字画像を再生成してください。");
    ImGui::TextWrapped("%s", editorStatus_.c_str());
    if (isActive_ && !isSliding_) {
        SetPageOffset(currentPage_, 0.0f);
    }
#endif
}

bool ControlsGuideOverlay::IsToggleHeld() const {
    // キーボードとゲームパッドの開閉入力をまとめ、開いた直後の再入力を抑制する。
    if (!inputManager_) {
        return false;
    }

    return inputManager_->IsKeyPressed(DIK_TAB) ||
           inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_B) ||
           inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_BACK);
}

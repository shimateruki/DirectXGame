#define NOMINMAX
#include "ControlsGuideOverlay.h"

#include "InputManager.h"
#include "Player.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteLayoutScaler.h"
#include "TextureManager.h"

#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace {
Vector4 ReadColor(const json& value, const Vector4& fallback) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }

    Vector4 color = fallback;
    color.x = value[0].get<float>();
    color.y = value[1].get<float>();
    color.z = value[2].get<float>();
    if (value.size() >= 4) {
        color.w = value[3].get<float>();
    }
    return color;
}

float SmoothStep(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

struct AbilityGuideVisual {
    std::array<const char*, 3> labelTextures{};
};

AbilityGuideVisual ResolveAbilityGuide(const Player* player) {
    if (!player) {
        return {};
    }

    if (!player->IsEnemyMorphed()) {
        return { {
            "ui/control_guide/labels/absorb.png",
            "ui/control_guide/labels/throw.png",
            nullptr
        } };
    }

    switch (player->GetEnemyMorphType()) {
    case Player::EnemyMorphType::Slime:
        return { {
            "ui/control_guide/labels/slime_dive.png",
            "ui/control_guide/labels/puni_straight.png",
            "ui/control_guide/labels/bounce_evade.png"
        } };
    case Player::EnemyMorphType::Bomber:
        return { {
            "ui/control_guide/labels/bomb_throw.png",
            "ui/control_guide/labels/bomb_place.png",
            "ui/control_guide/labels/blast_jump.png"
        } };
    case Player::EnemyMorphType::FireSlime:
        return { {
            "ui/control_guide/labels/fireball.png",
            "ui/control_guide/labels/flame_breath.png",
            "ui/control_guide/labels/blaze_step.png"
        } };
    case Player::EnemyMorphType::ThunderSlime:
        return { {
            "ui/control_guide/labels/thunder_chain.png",
            "ui/control_guide/labels/charged_discharge.png",
            "ui/control_guide/labels/thunder_step.png"
        } };
    case Player::EnemyMorphType::WindSlime:
        return { {
            "ui/control_guide/labels/updraft.png",
            "ui/control_guide/labels/wind_breath.png",
            "ui/control_guide/labels/wind_dash.png"
        } };
    default:
        return {};
    }
}

const char* ResolvePortraitTexture(const Player* player) {
    if (!player || !player->IsEnemyMorphed()) {
        return "ui/portraits/player.png";
    }

    switch (player->GetEnemyMorphType()) {
    case Player::EnemyMorphType::Slime:
        return "ui/control_guide/portraits/pink_slime.png";
    case Player::EnemyMorphType::Bomber:
        return "ui/control_guide/portraits/bomb_slime.png";
    case Player::EnemyMorphType::Bat:
        return "ui/portraits/bat.png";
    case Player::EnemyMorphType::BeamDrone:
        return "ui/portraits/beam_drone.png";
    case Player::EnemyMorphType::Mushroom:
        return "ui/portraits/mushroom.png";
    case Player::EnemyMorphType::GiantSlime:
        return "ui/portraits/giant_slime.png";
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

void SetFullTextureRect(Sprite* sprite, uint32_t textureHandle) {
    if (!sprite || textureHandle == 0) {
        return;
    }

    sprite->SetTextureHandle(textureHandle);
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle);
    sprite->SetTextureRect(
        { 0.0f, 0.0f },
        { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
}
}

void ControlsGuideOverlay::Initialize(
    SpriteCommon* spriteCommon,
    Player* player,
    const std::string& layoutPath) {
    spriteCommon_ = spriteCommon;
    inputManager_ = InputManager::GetInstance();
    player_ = player;
    LoadLayout(layoutPath);
    BindLayoutSprites();
    RefreshAbilityVisuals();
    SetAllVisible(false);
}

void ControlsGuideOverlay::Finalize() {
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
    isSliding_ = false;
    portraitTexturePath_.clear();
    abilityLabelTexturePaths_ = {};
    abilityActionAvailable_ = {};
}

void ControlsGuideOverlay::SetActive(bool active) {
    if (isActive_ == active) {
        return;
    }

    isActive_ = active;
    suppressToggleInput_ = active;
    isSliding_ = false;
    slideTimer_ = 0.0f;
    outgoingPage_ = currentPage_;
    incomingPage_ = currentPage_;
    RefreshAbilityVisuals();
    SetAllVisible(active);
    if (active) {
        SetPageVisible(currentPage_, true);
        SetPageOffset(currentPage_, 0.0f);
        UpdateStaticVisuals();
    }
}

void ControlsGuideOverlay::Update(float deltaTime) {
    if (!isActive_) {
        return;
    }

    time_ += deltaTime;
    RefreshAbilityVisuals();

    if (suppressToggleInput_) {
        if (!IsToggleHeld()) {
            suppressToggleInput_ = false;
        }
    } else if (inputManager_) {
        if (inputManager_->IsKeyTriggered(DIK_TAB) ||
            inputManager_->IsKeyTriggered(DIK_BACK) ||
            inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B) ||
            inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_BACK)) {
            SetActive(false);
            return;
        }

        if (!isSliding_) {
            const bool left =
                inputManager_->IsKeyTriggered(DIK_LEFT) ||
                inputManager_->IsKeyTriggered(DIK_A) ||
                inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT);
            const bool right =
                inputManager_->IsKeyTriggered(DIK_RIGHT) ||
                inputManager_->IsKeyTriggered(DIK_D) ||
                inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT);

            if (left != right) {
                StartSlide(left ? -1 : 1);
            }
        }
    }

    UpdateSlide(deltaTime);
    UpdateStaticVisuals();

    for (auto& sprite : sprites_) {
        if (sprite && sprite->IsVisible()) {
            sprite->Update();
        }
    }
}

void ControlsGuideOverlay::Draw() {
    if (!isActive_) {
        return;
    }

    for (auto& sprite : sprites_) {
        if (sprite && sprite->IsVisible()) {
            sprite->Draw();
        }
    }
}

void ControlsGuideOverlay::CollectReplaySprites(std::vector<Sprite*>& replaySprites) const {
    replaySprites.reserve(replaySprites.size() + sprites_.size());
    for (const auto& sprite : sprites_) {
        if (sprite) {
            replaySprites.push_back(sprite.get());
        }
    }
}

void ControlsGuideOverlay::CaptureReplayState(json& state) const {
    state = {
        { "active", isActive_ },
        { "suppressInput", suppressToggleInput_ },
        { "sliding", isSliding_ },
        { "currentPage", currentPage_ },
        { "outgoingPage", outgoingPage_ },
        { "incomingPage", incomingPage_ },
        { "slideDirection", slideDirection_ },
        { "slideTimer", slideTimer_ },
        { "time", time_ }
    };
}

void ControlsGuideOverlay::RestoreReplayState(const json& state) {
    if (!state.is_object()) {
        return;
    }

    isActive_ = state.value("active", isActive_);
    suppressToggleInput_ = state.value("suppressInput", suppressToggleInput_);
    isSliding_ = state.value("sliding", isSliding_);
    currentPage_ = std::clamp(state.value("currentPage", currentPage_), 0, kPageCount - 1);
    outgoingPage_ = std::clamp(state.value("outgoingPage", outgoingPage_), 0, kPageCount - 1);
    incomingPage_ = std::clamp(state.value("incomingPage", incomingPage_), 0, kPageCount - 1);
    slideDirection_ = state.value("slideDirection", slideDirection_) < 0 ? -1 : 1;
    slideTimer_ = std::max(0.0f, state.value("slideTimer", slideTimer_));
    time_ = state.value("time", time_);

    RefreshAbilityVisuals();
    SetAllVisible(isActive_);
    if (isActive_) {
        UpdateSlide(0.0f);
        UpdateStaticVisuals();
    }
}

void ControlsGuideOverlay::LoadLayout(const std::string& layoutPath) {
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
    for (const auto& spriteData : root["sprites"]) {
        const std::string name = spriteData.value("name", "controls_guide_sprite");
        const std::string texture = spriteData.value("texture", "common/white.png");
        const uint32_t textureHandle = Sprite::LoadTexture(texture.empty() ? "common/white.png" : texture);

        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_, textureHandle);
        sprite->SetName(name);
        sprite->SetTextureName(texture);
        sprite->SetPosition(SpriteLayoutScaler::ScalePosition(
            SpriteLayoutScaler::ReadVector2(spriteData.value("position", json::array()), sprite->GetPosition()),
            layoutScale));
        sprite->SetSize(SpriteLayoutScaler::ScaleSize(
            SpriteLayoutScaler::ReadVector2(spriteData.value("size", json::array()), sprite->GetSize()),
            layoutScale));
        sprite->SetAnchorPoint(SpriteLayoutScaler::ReadVector2(
            spriteData.value("anchor", json::array()), sprite->GetAnchorPoint()));
        sprite->SetColor(ReadColor(spriteData.value("color", json::array()), sprite->GetColor()));
        sprite->SetEmissive(spriteData.value("emissive", 1.0f));
        sprite->SetVisible(false);
        sprite->Update();
        sprites_.push_back(std::move(sprite));
    }
}

void ControlsGuideOverlay::BindLayoutSprites() {
    background_ = BindElement("controls_guide_background");
    leftArrow_ = BindElement("controls_guide_left_arrow");
    rightArrow_ = BindElement("controls_guide_right_arrow");

    for (int page = 0; page < kPageCount; ++page) {
        const std::string prefix = "controls_guide_page_" + std::to_string(page);
        pages_[static_cast<size_t>(page)].panel = BindElement(prefix + "_panel");
        pages_[static_cast<size_t>(page)].portrait = BindElement(prefix + "_portrait");
        for (int action = 0; action < kActionCount; ++action) {
            pages_[static_cast<size_t>(page)].actionGlyphs[static_cast<size_t>(action)] =
                BindElement(prefix + "_action_" + std::to_string(action));
            pages_[static_cast<size_t>(page)].actionLabels[static_cast<size_t>(action)] =
                BindElement(prefix + "_action_label_" + std::to_string(action));
        }
        pageDots_[static_cast<size_t>(page)] =
            BindElement("controls_guide_page_dot_" + std::to_string(page));
    }
}

Sprite* ControlsGuideOverlay::FindSprite(const std::string& name) const {
    for (const auto& sprite : sprites_) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr;
}

ControlsGuideOverlay::Element ControlsGuideOverlay::BindElement(const std::string& name) const {
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
    if (pageIndex < 0 || pageIndex >= kPageCount) {
        return;
    }

    PageVisual& page = pages_[static_cast<size_t>(pageIndex)];
    if (page.panel.sprite) page.panel.sprite->SetVisible(visible);
    if (page.portrait.sprite) page.portrait.sprite->SetVisible(visible);
    for (int action = 0; action < kActionCount; ++action) {
        const bool actionVisible = visible &&
            (pageIndex != 1 || abilityActionAvailable_[static_cast<size_t>(action)]);
        Element& glyph = page.actionGlyphs[static_cast<size_t>(action)];
        Element& label = page.actionLabels[static_cast<size_t>(action)];
        if (glyph.sprite) glyph.sprite->SetVisible(actionVisible);
        if (label.sprite) label.sprite->SetVisible(actionVisible);
    }
}

void ControlsGuideOverlay::SetPageOffset(int pageIndex, float offsetX) {
    if (pageIndex < 0 || pageIndex >= kPageCount) {
        return;
    }

    PageVisual& page = pages_[static_cast<size_t>(pageIndex)];
    const auto applyOffset = [offsetX](Element& element) {
        if (element.sprite) {
            element.sprite->SetPosition({ element.basePosition.x + offsetX, element.basePosition.y });
        }
    };

    applyOffset(page.panel);
    applyOffset(page.portrait);
    for (Element& glyph : page.actionGlyphs) {
        applyOffset(glyph);
    }
    for (Element& label : page.actionLabels) {
        applyOffset(label);
    }
}

void ControlsGuideOverlay::StartSlide(int direction) {
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
    if (!isSliding_) {
        SetPageOffset(currentPage_, 0.0f);
        return;
    }

    slideTimer_ += std::max(0.0f, deltaTime);
    const float rate = slideDuration_ > 0.0f
        ? std::clamp(slideTimer_ / slideDuration_, 0.0f, 1.0f)
        : 1.0f;
    const float eased = SmoothStep(rate);
    const float slideDistance = background_.baseSize.x > 0.0f
        ? background_.baseSize.x * 0.86f
        : 1650.0f;

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
    const float pulse = 0.5f + 0.5f * std::sin(time_ * 5.2f);

    const auto updateArrow = [pulse](Element& arrow, float phase) {
        if (!arrow.sprite) {
            return;
        }
        const float scale = 1.0f + pulse * 0.08f;
        arrow.sprite->SetSize({ arrow.baseSize.x * scale, arrow.baseSize.y * scale });
        arrow.sprite->SetPosition({
            arrow.basePosition.x + std::sin(phase) * 3.0f,
            arrow.basePosition.y
        });
        arrow.sprite->SetColor({ 1.0f, 0.96f, 0.62f + pulse * 0.18f, 1.0f });
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
        dot.sprite->SetSize({ dot.baseSize.x * scale, dot.baseSize.y * scale });
        dot.sprite->SetColor(selected
            ? Vector4{ 1.0f, 0.86f, 0.28f, 1.0f }
            : Vector4{ 0.65f, 0.90f, 1.0f, 0.72f });
    }
}

void ControlsGuideOverlay::RefreshAbilityVisuals() {
    const std::string nextPath = ResolvePortraitTexture(player_);
    if (nextPath != portraitTexturePath_) {
        portraitTexturePath_ = nextPath;
        Sprite* portrait = pages_[1].portrait.sprite;
        if (portrait) {
            const uint32_t textureHandle = Sprite::LoadTexture(nextPath);
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
        Sprite* label = pages_[1].actionLabels[static_cast<size_t>(action)].sprite;
        if (label && available) {
            const uint32_t textureHandle = Sprite::LoadTexture(nextLabelPath);
            SetFullTextureRect(label, textureHandle);
            label->SetTextureName(nextLabelPath);
        }
    }

    if (!guideChanged || !isActive_) {
        return;
    }

    const bool abilityPageVisible = isSliding_
        ? (outgoingPage_ == 1 || incomingPage_ == 1)
        : currentPage_ == 1;
    SetPageVisible(1, abilityPageVisible);
}

bool ControlsGuideOverlay::IsToggleHeld() const {
    if (!inputManager_) {
        return false;
    }

    return inputManager_->IsKeyPressed(DIK_TAB) ||
        inputManager_->IsKeyPressed(DIK_BACK) ||
        inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_B) ||
        inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_BACK);
}

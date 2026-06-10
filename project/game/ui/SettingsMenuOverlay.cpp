#define NOMINMAX
#include "SettingsMenuOverlay.h"

#include "GameSettingsManager.h"
#include "InputManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace {
constexpr float kAdjustRepeatFirst = 0.28f;
constexpr float kAdjustRepeatNext = 0.07f;
constexpr float kSelectedKnobScale = 1.14f;
constexpr float kNumberRightPadding = 74.0f;

Vector4 LerpColor(const Vector4& a, const Vector4& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    };
}

Vector2 ReadVector2(const json& value, const Vector2& fallback) {
    if (!value.is_array() || value.size() < 2) {
        return fallback;
    }
    return { value[0].get<float>(), value[1].get<float>() };
}

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
}

void SettingsMenuOverlay::Initialize(SpriteCommon* spriteCommon, const std::string& layoutPath) {
    spriteCommon_ = spriteCommon;
    inputManager_ = InputManager::GetInstance();
    GameSettingsManager::GetInstance()->Load();
    LoadLayout(layoutPath);
    BindLayoutSprites();
    SetAllVisible(false);
}

void SettingsMenuOverlay::Finalize() {
    GameSettingsManager::GetInstance()->Save();
    sprites_.clear();
    background_ = nullptr;
    panel_ = nullptr;
    title_ = nullptr;
    hintLine_ = nullptr;
    rows_ = {};
    spriteCommon_ = nullptr;
    inputManager_ = nullptr;
    isActive_ = false;
}

void SettingsMenuOverlay::SetActive(bool active) {
    if (isActive_ == active) {
        return;
    }

    isActive_ = active;
    suppressCloseInput_ = active;
    repeatTimer_ = 0.0f;
    repeatDirection_ = 0;

    if (active) {
        GameSettingsManager::GetInstance()->Load();
    } else {
        GameSettingsManager::GetInstance()->Save();
    }

    SetAllVisible(active);
}

SettingsMenuOverlay::Result SettingsMenuOverlay::Update(float deltaTime) {
    if (!isActive_) {
        return Result::None;
    }

    sceneTime_ += deltaTime;
    Result result = UpdateInput(deltaTime);
    UpdateSprites();

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Update();
        }
    }

    return result;
}

void SettingsMenuOverlay::Draw() {
    if (!isActive_) {
        return;
    }

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Draw();
        }
    }
}

void SettingsMenuOverlay::LoadLayout(const std::string& layoutPath) {
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

    for (const auto& spriteData : root["sprites"]) {
        const std::string name = spriteData.value("name", "settings_sprite");
        const std::string texture = spriteData.value("texture", "common/white.png");
        uint32_t textureHandle = Sprite::LoadTexture(texture.empty() ? "common/white.png" : texture);

        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_, textureHandle);
        sprite->SetName(name);
        sprite->SetTextureName(texture);
        sprite->SetPosition(ReadVector2(spriteData.value("position", json::array()), sprite->GetPosition()));
        sprite->SetSize(ReadVector2(spriteData.value("size", json::array()), sprite->GetSize()));
        sprite->SetAnchorPoint(ReadVector2(spriteData.value("anchor", json::array()), sprite->GetAnchorPoint()));
        sprite->SetColor(ReadColor(spriteData.value("color", json::array()), sprite->GetColor()));
        sprite->SetEmissive(spriteData.value("emissive", 1.0f));
        sprite->SetVisible(false);
        sprite->Update();

        sprites_.push_back(std::move(sprite));
    }
}

void SettingsMenuOverlay::BindLayoutSprites() {
    background_ = FindSprite("settings_background");
    panel_ = FindSprite("settings_panel");
    title_ = FindSprite("settings_title");
    hintLine_ = FindSprite("settings_hint_line");

    constexpr std::array<const char*, 3> prefixes = {
        "settings_bgm",
        "settings_se",
        "settings_camera"
    };

    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        const std::string prefix = prefixes[static_cast<size_t>(i)];
        OptionRow& row = rows_[static_cast<size_t>(i)];
        row.backdrop = FindSprite(prefix + "_row");
        row.label = FindSprite(prefix + "_label");
        row.track = FindSprite(prefix + "_track");
        row.fill = FindSprite(prefix + "_fill");
        row.knob = FindSprite(prefix + "_knob");
        if (row.fill) {
            row.fill->SetAnchorPoint({ 0.0f, 0.5f });
        }

        for (int digit = 0; digit < 3; ++digit) {
            row.digits[static_cast<size_t>(digit)] = FindSprite(prefix + "_value" + std::to_string(digit));
        }
    }
}

Sprite* SettingsMenuOverlay::FindSprite(const std::string& name) const {
    for (const auto& sprite : sprites_) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr;
}

void SettingsMenuOverlay::SetAllVisible(bool visible) {
    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->SetVisible(visible);
        }
    }
}

bool SettingsMenuOverlay::IsDecisionOrBackHeld() const {
    if (!inputManager_) {
        return false;
    }

    return inputManager_->IsKeyPressed(DIK_SPACE) ||
        inputManager_->IsKeyPressed(DIK_RETURN) ||
        inputManager_->IsKeyPressed(DIK_BACK) ||
        inputManager_->IsKeyPressed(DIK_ESCAPE) ||
        inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_B);
}

SettingsMenuOverlay::Result SettingsMenuOverlay::UpdateInput(float deltaTime) {
    if (!inputManager_) {
        return Result::None;
    }

    if (suppressCloseInput_) {
        if (!IsDecisionOrBackHeld()) {
            suppressCloseInput_ = false;
        }
        return Result::None;
    }

    const bool up =
        inputManager_->IsKeyTriggered(DIK_UP) ||
        inputManager_->IsKeyTriggered(DIK_W) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP);
    const bool down =
        inputManager_->IsKeyTriggered(DIK_DOWN) ||
        inputManager_->IsKeyTriggered(DIK_S) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN);

    if (up) {
        ChangeSelection(-1);
    }
    if (down) {
        ChangeSelection(1);
    }

    const bool leftPressed =
        inputManager_->IsKeyPressed(DIK_LEFT) ||
        inputManager_->IsKeyPressed(DIK_A) ||
        inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_DPAD_LEFT);
    const bool rightPressed =
        inputManager_->IsKeyPressed(DIK_RIGHT) ||
        inputManager_->IsKeyPressed(DIK_D) ||
        inputManager_->IsGamepadButtonPressed(XINPUT_GAMEPAD_DPAD_RIGHT);

    const bool leftTriggered =
        inputManager_->IsKeyTriggered(DIK_LEFT) ||
        inputManager_->IsKeyTriggered(DIK_A) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT);
    const bool rightTriggered =
        inputManager_->IsKeyTriggered(DIK_RIGHT) ||
        inputManager_->IsKeyTriggered(DIK_D) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT);

    int direction = 0;
    bool triggered = false;
    if (leftPressed && !rightPressed) {
        direction = -1;
        triggered = leftTriggered;
    } else if (rightPressed && !leftPressed) {
        direction = 1;
        triggered = rightTriggered;
    }

    if (direction != 0) {
        repeatTimer_ -= deltaTime;
        if (triggered || direction != repeatDirection_ || repeatTimer_ <= 0.0f) {
            AdjustSelectedValue(direction);
            repeatTimer_ = triggered || direction != repeatDirection_ ? kAdjustRepeatFirst : kAdjustRepeatNext;
            repeatDirection_ = direction;
        }
    } else {
        repeatTimer_ = 0.0f;
        repeatDirection_ = 0;
    }

    if (inputManager_->IsKeyTriggered(DIK_BACK) ||
        inputManager_->IsKeyTriggered(DIK_ESCAPE) ||
        inputManager_->IsKeyTriggered(DIK_RETURN) ||
        inputManager_->IsKeyTriggered(DIK_SPACE) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
        SetActive(false);
        return Result::Closed;
    }

    return Result::None;
}

void SettingsMenuOverlay::UpdateSprites() {
    const Vector4 normalText = { 0.78f, 0.92f, 0.96f, 0.78f };
    const Vector4 selectedText = { 0.30f, 0.86f, 1.0f, 1.0f };
    const Vector4 fillNormal = { 0.22f, 0.78f, 0.95f, 0.88f };
    const Vector4 fillSelected = { 0.25f, 0.84f, 1.0f, 0.98f };

    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        OptionRow& row = rows_[static_cast<size_t>(i)];
        const bool selected = i == selectedIndex_;
        const float pulse = selected ? (0.5f + 0.5f * std::sin(sceneTime_ * 5.5f)) : 0.0f;
        const float normalized = GetNormalizedValue(static_cast<Item>(i));

        Vector2 trackPosition = row.track ? row.track->GetPosition() : Vector2{ 0.0f, 0.0f };
        Vector2 trackSize = row.track ? row.track->GetSize() : Vector2{ 1.0f, 1.0f };
        Vector2 trackAnchor = row.track ? row.track->GetAnchorPoint() : Vector2{ 0.5f, 0.5f };
        const float sliderLeft = trackPosition.x - trackSize.x * trackAnchor.x;
        const float fillWidth = std::max(trackSize.y * 0.6f, trackSize.x * normalized);

        if (row.backdrop) {
            const uint32_t handle = Sprite::LoadTexture(selected ? "ui/settings/settings_row_selected.png" : "ui/settings/settings_row.png");
            row.backdrop->SetTextureHandle(handle);
            const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
            row.backdrop->SetTextureRect({ 0.0f, 0.0f }, { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
            row.backdrop->SetColor(selected ? Vector4{ 1.0f, 1.0f, 1.0f, 0.96f } : Vector4{ 1.0f, 1.0f, 1.0f, 0.82f });
        }
        if (row.label) {
            row.label->SetColor(selected ? selectedText : normalText);
        }
        if (row.fill) {
            row.fill->SetPosition({ sliderLeft, trackPosition.y });
            row.fill->SetSize({ fillWidth, trackSize.y });
            row.fill->SetColor(selected ? LerpColor(fillSelected, { 0.70f, 1.0f, 1.0f, 1.0f }, pulse * 0.45f) : fillNormal);
        }
        if (row.knob) {
            const Vector2 knobBaseSize = row.track ? Vector2{ trackSize.y * 1.7f, trackSize.y * 1.7f } : row.knob->GetSize();
            const float knobScale = selected ? kSelectedKnobScale : 1.0f;
            row.knob->SetPosition({ sliderLeft + trackSize.x * normalized, trackPosition.y });
            row.knob->SetSize({ knobBaseSize.x * knobScale, knobBaseSize.y * knobScale });
            row.knob->SetColor(selected ? Vector4{ 0.75f, 1.0f, 1.0f, 1.0f } : Vector4{ 0.84f, 0.96f, 1.0f, 0.9f });
        }

        Vector2 valueRightPosition = trackPosition;
        if (row.backdrop) {
            const Vector2 backdropPosition = row.backdrop->GetPosition();
            const Vector2 backdropSize = row.backdrop->GetSize();
            const Vector2 backdropAnchor = row.backdrop->GetAnchorPoint();
            valueRightPosition = {
                backdropPosition.x + backdropSize.x * (1.0f - backdropAnchor.x) - kNumberRightPadding,
                trackPosition.y
            };
        }

        const float digitHeight = std::max(34.0f, trackSize.y * (selected ? 1.42f : 1.26f));
        SetNumberSprites(row, GetDisplayValue(static_cast<Item>(i)), valueRightPosition, digitHeight, selected ? selectedText : normalText);
    }
}

void SettingsMenuOverlay::ChangeSelection(int direction) {
    const int count = static_cast<int>(Item::Count);
    selectedIndex_ = (selectedIndex_ + direction + count) % count;
    repeatTimer_ = 0.0f;
    repeatDirection_ = 0;
}

void SettingsMenuOverlay::AdjustSelectedValue(int direction) {
    GameSettingsManager* settings = GameSettingsManager::GetInstance();
    const Item item = static_cast<Item>(selectedIndex_);
    const float sign = static_cast<float>(direction);

    switch (item) {
    case Item::BGM:
        settings->SetBGMVolume(settings->GetBGMVolume() + sign * 0.05f);
        break;
    case Item::SE:
        settings->SetSEVolume(settings->GetSEVolume() + sign * 0.05f);
        break;
    case Item::CameraSensitivity:
        settings->SetCameraSensitivity(settings->GetCameraSensitivity() + sign * 0.05f);
        break;
    default:
        break;
    }

    settings->Save();
}

float SettingsMenuOverlay::GetValue(Item item) const {
    GameSettingsManager* settings = GameSettingsManager::GetInstance();
    switch (item) {
    case Item::BGM:
        return settings->GetBGMVolume();
    case Item::SE:
        return settings->GetSEVolume();
    case Item::CameraSensitivity:
        return settings->GetCameraSensitivity();
    default:
        return 0.0f;
    }
}

float SettingsMenuOverlay::GetNormalizedValue(Item item) const {
    if (item == Item::CameraSensitivity) {
        return std::clamp((GetValue(item) - 0.5f) / 1.5f, 0.0f, 1.0f);
    }
    return std::clamp(GetValue(item), 0.0f, 1.0f);
}

int SettingsMenuOverlay::GetDisplayValue(Item item) const {
    return static_cast<int>(std::round(GetValue(item) * 100.0f));
}

void SettingsMenuOverlay::SetNumberSprites(OptionRow& row, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color) {
    value = std::clamp(value, 0, 999);

    const int hundreds = value / 100;
    const int tens = (value / 10) % 10;
    const int ones = value % 10;
    const std::array<int, 3> digits = { hundreds, tens, ones };
    const int digitCount = value >= 100 ? 3 : (value >= 10 ? 2 : 1);
    const float digitWidth = digitHeight * 0.68f;
    const float spacing = digitWidth * 0.82f;
    const float totalWidth = digitCount == 1 ? digitWidth : spacing * static_cast<float>(digitCount - 1) + digitWidth;
    const float startX = rightAlignedPosition.x - totalWidth + digitWidth * 0.5f;

    for (int i = 0; i < 3; ++i) {
        Sprite* sprite = row.digits[static_cast<size_t>(i)];
        if (!sprite) {
            continue;
        }

        const int firstVisible = 3 - digitCount;
        const bool visible = isActive_ && i >= firstVisible;
        sprite->SetVisible(visible);
        if (!visible) {
            continue;
        }

        const int displayIndex = i - firstVisible;
        sprite->SetTextureHandle(Sprite::LoadTexture("number/" + std::to_string(digits[static_cast<size_t>(i)]) + ".png"));
        sprite->SetPosition({ startX + spacing * static_cast<float>(displayIndex), rightAlignedPosition.y });
        sprite->SetSize({ digitWidth, digitHeight });
        sprite->SetColor(color);
    }
}

#define NOMINMAX
#include "SettingsScene.h"

#include "AudioPlayer.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "GameSettingsManager.h"
#include "InputManager.h"
#include "LevelLoader.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "TextureManager.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr const char* kSettingsSpriteLayoutPath = "Resources/json/sprite/settingsScene.json";
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
}

void SettingsScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    GameSettingsManager::GetInstance()->Load();

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);
    levelLoader_ = std::make_unique<LevelLoader>();

    InitializeSprites();

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/Alarm02.mp3");
    audioPlayer_->PlayBGM(bgmHandle_, true, 0.65f);

    DebugConsole::GetInstance()->AddLog("[Settings] Open settings scene.");
}

void SettingsScene::Finalize() {
    GameSettingsManager::GetInstance()->Save();
    audioPlayer_->StopBGM();
    sprites_.clear();
    levelLoader_.reset();
    spriteCommon_.reset();
}

void SettingsScene::Update(float deltaTime) {
    sceneTime_ += deltaTime;
    UpdateInput(deltaTime);
    UpdateSprites(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }
}

void SettingsScene::Draw() {}

void SettingsScene::DrawUI() {
    if (!spriteCommon_) {
        return;
    }

    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (auto& sprite : sprites_) {
        sprite->Draw();
    }
}

void SettingsScene::InitializeSprites() {
    if (levelLoader_) {
        levelLoader_->LoadSpriteLayout(this, kSettingsSpriteLayoutPath);
    }
    BindLayoutSprites();
}

void SettingsScene::BindLayoutSprites() {
    background_ = FindSprite("settings_background");
    panel_ = FindSprite("settings_panel");
    title_ = FindSprite("settings_title");
    hintLine_ = FindSprite("settings_hint_line");
    slimeCursor_ = FindSprite("settings_slime_cursor");
    footerButtons_[static_cast<size_t>(FooterAction::Back)] = FindSprite("settings_footer_back_button");
    footerButtons_[static_cast<size_t>(FooterAction::Reset)] = FindSprite("settings_footer_reset_button");
    footerButtons_[static_cast<size_t>(FooterAction::Apply)] = FindSprite("settings_footer_apply_button");
    footerTexts_[static_cast<size_t>(FooterAction::Back)] = FindSprite("settings_footer_back_text");
    footerTexts_[static_cast<size_t>(FooterAction::Reset)] = FindSprite("settings_footer_reset_text");
    footerTexts_[static_cast<size_t>(FooterAction::Apply)] = FindSprite("settings_footer_apply_text");
    categoryTabs_[0] = FindSprite("settings_category_sound_tab");
    categoryTabs_[1] = FindSprite("settings_category_control_tab");
    categoryLabels_[0] = FindSprite("settings_category_sound_label");
    categoryLabels_[1] = FindSprite("settings_category_control_label");
    categoryPointer_ = FindSprite("settings_category_pointer");

    constexpr std::array<const char*, static_cast<size_t>(Item::Count)> rowPrefixes = {
        "settings_bgm",
        "settings_se",
        "settings_camera"
    };

    const std::array<std::string, 3> hiddenPrefixes = {
        "settings_brightness",
        "settings_resolution",
        "settings_fullscreen"
    };
    const std::array<std::string, 9> hiddenParts = {
        "_row",
        "_label",
        "_track",
        "_fill",
        "_knob",
        "_arrow_left",
        "_arrow_right",
        "_value_label",
        "_value0"
    };

    if (Sprite* screenTab = FindSprite("settings_category_screen_tab")) {
        if (Sprite* controlTab = categoryTabs_[static_cast<size_t>(Category::Control)]) {
            controlTab->SetPosition(screenTab->GetPosition());
        }
        screenTab->SetVisible(false);
    }
    if (Sprite* screenLabel = FindSprite("settings_category_screen_label")) {
        if (Sprite* controlLabel = categoryLabels_[static_cast<size_t>(Category::Control)]) {
            controlLabel->SetPosition(screenLabel->GetPosition());
        }
        screenLabel->SetVisible(false);
    }
    for (const std::string& prefix : hiddenPrefixes) {
        for (const std::string& part : hiddenParts) {
            if (Sprite* sprite = FindSprite(prefix + part)) {
                sprite->SetVisible(false);
            }
        }
        for (int digit = 1; digit < 3; ++digit) {
            if (Sprite* sprite = FindSprite(prefix + "_value" + std::to_string(digit))) {
                sprite->SetVisible(false);
            }
        }
    }

    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        const std::string prefix = rowPrefixes[static_cast<size_t>(i)];
        OptionRow& row = rows_[static_cast<size_t>(i)];
        row.backdrop = FindSprite(prefix + "_row");
        row.label = FindSprite(prefix + "_label");
        row.track = FindSprite(prefix + "_track");
        row.fill = FindSprite(prefix + "_fill");
        row.knob = FindSprite(prefix + "_knob");
        row.leftArrow = FindSprite(prefix + "_arrow_left");
        row.rightArrow = FindSprite(prefix + "_arrow_right");
        row.valueLabel = FindSprite(prefix + "_value_label");

        if (row.fill) {
            row.fill->SetAnchorPoint({ 0.0f, 0.5f });
        }

        for (int digit = 0; digit < 3; ++digit) {
            row.digits[static_cast<size_t>(digit)] =
                FindSprite(prefix + "_value" + std::to_string(digit));
        }
    }

    if (slimeCursor_) {
        slimeCursor_->SetTextureRect({ 115.0f, 218.0f }, { 1024.0f, 827.0f });
        slimeCursorBaseSize_ = slimeCursor_->GetSize();
        if (rows_[0].backdrop) {
            const Vector2 rowPosition = rows_[0].backdrop->GetPosition();
            const Vector2 cursorPosition = slimeCursor_->GetPosition();
            slimeCursorOffset_ = { cursorPosition.x - rowPosition.x, cursorPosition.y - rowPosition.y };
        }
    }
}

Sprite* SettingsScene::FindSprite(const std::string& name) const {
    for (const auto& sprite : sprites_) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr;
}

void SettingsScene::UpdateInput(float deltaTime) {
    const bool up =
        inputManager_->IsKeyTriggered(DIK_UP) ||
        inputManager_->IsKeyTriggered(DIK_W) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP);
    const bool down =
        inputManager_->IsKeyTriggered(DIK_DOWN) ||
        inputManager_->IsKeyTriggered(DIK_S) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN);

    if (up) {
        if (focusArea_ == FocusArea::Footer) {
            focusArea_ = FocusArea::Items;
            selectedIndex_ = static_cast<int>(Item::Count) - 1;
        } else {
            ChangeSelection(-1);
        }
    }
    if (down) {
        if (focusArea_ == FocusArea::Items && selectedIndex_ == static_cast<int>(Item::Count) - 1) {
            focusArea_ = FocusArea::Footer;
            repeatTimer_ = 0.0f;
            repeatDirection_ = 0;
        } else if (focusArea_ == FocusArea::Items) {
            ChangeSelection(1);
        }
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

    if (focusArea_ == FocusArea::Footer && (leftTriggered || rightTriggered)) {
        ChangeFooterSelection(leftTriggered ? -1 : 1);
        direction = 0;
    }

    if (focusArea_ == FocusArea::Items && direction != 0) {
        const Item currentItem = static_cast<Item>(selectedIndex_);
        if (IsChoiceItem(currentItem)) {
            if (triggered) {
                AdjustSelectedValue(direction);
            }
            repeatTimer_ = 0.0f;
            repeatDirection_ = 0;
        } else {
            repeatTimer_ -= deltaTime;
            if (triggered || direction != repeatDirection_ || repeatTimer_ <= 0.0f) {
                AdjustSelectedValue(direction);
                repeatTimer_ = triggered || direction != repeatDirection_ ? kAdjustRepeatFirst : kAdjustRepeatNext;
                repeatDirection_ = direction;
            }
        }
    } else {
        repeatTimer_ = 0.0f;
        repeatDirection_ = 0;
    }

    if (inputManager_->IsKeyTriggered(DIK_BACK) ||
        inputManager_->IsKeyTriggered(DIK_ESCAPE) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
        ReturnToTitle();
    }

    if (focusArea_ == FocusArea::Footer &&
        (inputManager_->IsKeyTriggered(DIK_RETURN) ||
            inputManager_->IsKeyTriggered(DIK_SPACE) ||
            inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A))) {
        ExecuteFooterAction();
    }
}

void SettingsScene::UpdateSprites(float deltaTime) {
    (void)deltaTime;

    const Vector4 normalText = { 0.11f, 0.18f, 0.22f, 0.98f };
    const Vector4 selectedText = { 0.04f, 0.13f, 0.18f, 1.0f };
    const Vector4 fillNormal = { 0.06f, 0.72f, 0.78f, 0.94f };
    const Vector4 fillSelected = { 0.03f, 0.82f, 0.88f, 1.0f };

    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        OptionRow& row = rows_[static_cast<size_t>(i)];
        const Item item = static_cast<Item>(i);
        const bool visible = IsItemVisible(item);
        SetRowVisible(row, visible);
        if (!visible) {
            continue;
        }

        const bool selected = focusArea_ == FocusArea::Items && i == selectedIndex_;
        const float pulse = selected ? (0.5f + 0.5f * std::sin(sceneTime_ * 5.5f)) : 0.0f;
        const float normalized = GetNormalizedValue(item);
        const bool sliderItem = IsSliderItem(item);
        const bool choiceItem = IsChoiceItem(item);

        Vector2 trackPosition = row.track ? row.track->GetPosition() : Vector2{ 0.0f, 0.0f };
        Vector2 trackSize = row.track ? row.track->GetSize() : Vector2{ 1.0f, 1.0f };
        Vector2 trackAnchor = row.track ? row.track->GetAnchorPoint() : Vector2{ 0.5f, 0.5f };
        const float sliderLeft = trackPosition.x - trackSize.x * trackAnchor.x;
        const float fillWidth = std::max(trackSize.y * 0.6f, trackSize.x * normalized);

        if (row.backdrop) {
            const uint32_t backdropHandle = Sprite::LoadTexture(selected ? "ui/settings/settings_row_selected.png" : "ui/settings/settings_row.png");
            row.backdrop->SetTextureHandle(backdropHandle);
            const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(backdropHandle);
            row.backdrop->SetTextureRect({ 0.0f, 0.0f }, { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
            row.backdrop->SetColor(selected ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } : Vector4{ 1.0f, 1.0f, 1.0f, 0.98f });
        }
        if (row.label) {
            row.label->SetColor(selected ? selectedText : normalText);
        }
        if (row.fill) {
            row.fill->SetVisible(sliderItem);
            row.fill->SetPosition({ sliderLeft, trackPosition.y });
            row.fill->SetSize({ fillWidth, trackSize.y });
            row.fill->SetColor(LerpColor(fillSelected, { 0.38f, 1.0f, 1.0f, 1.0f }, pulse * 0.45f));
            if (!selected) {
                row.fill->SetColor(fillNormal);
            }
        }
        if (row.knob) {
            row.knob->SetVisible(sliderItem);
            const Vector2 knobBaseSize = row.track ? Vector2{ trackSize.y * 1.7f, trackSize.y * 1.7f } : row.knob->GetSize();
            const float knobScale = selected ? kSelectedKnobScale : 1.0f;
            row.knob->SetPosition({ sliderLeft + trackSize.x * normalized, trackPosition.y });
            row.knob->SetSize({ knobBaseSize.x * knobScale, knobBaseSize.y * knobScale });
            row.knob->SetColor(selected ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } : Vector4{ 0.96f, 0.92f, 0.82f, 0.95f });
        }
        if (row.track) {
            row.track->SetVisible(sliderItem);
        }
        if (row.leftArrow) {
            row.leftArrow->SetColor(selected ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } : Vector4{ 1.0f, 1.0f, 1.0f, 0.88f });
        }
        if (row.rightArrow) {
            row.rightArrow->SetColor(selected ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } : Vector4{ 1.0f, 1.0f, 1.0f, 0.88f });
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

        if (row.valueLabel) {
            const std::string texture = GetDisplayTextTexture(item);
            if (!texture.empty()) {
                const uint32_t handle = Sprite::LoadTexture(texture);
                row.valueLabel->SetTextureHandle(handle);
                const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
                row.valueLabel->SetTextureRect({ 0.0f, 0.0f }, { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
            }
            if (choiceItem && row.leftArrow && row.rightArrow) {
                const Vector2 leftPosition = row.leftArrow->GetPosition();
                const Vector2 rightPosition = row.rightArrow->GetPosition();
                row.valueLabel->SetPosition({ (leftPosition.x + rightPosition.x) * 0.5f, trackPosition.y });
            }
            row.valueLabel->SetColor(selected ? selectedText : normalText);
        }

        const bool numeric = IsSliderItem(item);
        const float digitHeight = std::max(34.0f, trackSize.y * (selected ? 1.42f : 1.26f));
        SetNumberSprites(
            row,
            GetDisplayValue(item),
            valueRightPosition,
            digitHeight,
            selected ? selectedText : normalText
        );
        if (!numeric) {
            for (Sprite* digit : row.digits) {
                if (digit) {
                    digit->SetVisible(false);
                }
            }
        }
    }

    UpdateCategorySprites();
    UpdateFooterSprites();
}

void SettingsScene::UpdateCategorySprites() {
    const uint32_t normalHandle = Sprite::LoadTexture("ui/settings/settings_category_tab.png");
    const uint32_t selectedHandle = Sprite::LoadTexture("ui/settings/settings_category_tab_selected.png");
    const int activeCategory = static_cast<int>(GetItemCategory(static_cast<Item>(selectedIndex_)));

    for (int i = 0; i < static_cast<int>(Category::Count); ++i) {
        const bool active = i == activeCategory && focusArea_ == FocusArea::Items;
        Sprite* tab = categoryTabs_[static_cast<size_t>(i)];
        if (tab) {
            tab->SetTextureHandle(active ? selectedHandle : normalHandle);
            const auto& metadata = TextureManager::GetInstance()->GetMetadata(active ? selectedHandle : normalHandle);
            tab->SetTextureRect({ 0.0f, 0.0f }, { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
            tab->SetColor({ 1.0f, 1.0f, 1.0f, active ? 1.0f : 0.82f });
        }
        Sprite* label = categoryLabels_[static_cast<size_t>(i)];
        if (label) {
            label->SetColor(active
                ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }
                : Vector4{ 0.11f, 0.18f, 0.22f, 0.98f });
        }
    }

    if (categoryPointer_) {
        Sprite* activeTab = categoryTabs_[static_cast<size_t>(activeCategory)];
        if (activeTab) {
            const Vector2 tabPosition = activeTab->GetPosition();
            const Vector2 tabSize = activeTab->GetSize();
            categoryPointer_->SetPosition({ tabPosition.x - tabSize.x * 0.72f, tabPosition.y });
            if (slimeCursor_) {
                const float bob = std::sin(sceneTime_ * 7.0f) * 4.0f;
                const float pulse = 0.5f + 0.5f * std::sin(sceneTime_ * 5.5f);
                const float squash = 1.0f + pulse * 0.05f;
                slimeCursor_->SetPosition({ tabPosition.x - tabSize.x * 0.94f, tabPosition.y + bob });
                slimeCursor_->SetSize({
                    slimeCursorBaseSize_.x * squash,
                    slimeCursorBaseSize_.y * (1.04f - pulse * 0.04f)
                });
                slimeCursor_->SetColor({
                    1.0f,
                    1.0f,
                    1.0f,
                    focusArea_ == FocusArea::Items ? 0.96f : 0.55f
                });
            }
        }
        categoryPointer_->SetColor({
            1.0f,
            1.0f,
            1.0f,
            focusArea_ == FocusArea::Items ? 1.0f : 0.55f
        });
    } else if (slimeCursor_) {
        slimeCursor_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
    }
}

void SettingsScene::SetRowVisible(OptionRow& row, bool visible) {
    if (row.backdrop) row.backdrop->SetVisible(visible);
    if (row.label) row.label->SetVisible(visible);
    if (row.track) row.track->SetVisible(visible);
    if (row.fill) row.fill->SetVisible(visible);
    if (row.knob) row.knob->SetVisible(visible);
    if (row.leftArrow) row.leftArrow->SetVisible(visible);
    if (row.rightArrow) row.rightArrow->SetVisible(visible);
    if (row.valueLabel) row.valueLabel->SetVisible(visible);
    for (Sprite* digit : row.digits) {
        if (digit) {
            digit->SetVisible(visible);
        }
    }
}

void SettingsScene::ChangeSelection(int direction) {
    const int count = static_cast<int>(Item::Count);
    selectedIndex_ = (selectedIndex_ + direction + count) % count;
    repeatTimer_ = 0.0f;
    repeatDirection_ = 0;
}

void SettingsScene::ChangeFooterSelection(int direction) {
    const int count = static_cast<int>(FooterAction::Count);
    footerIndex_ = (footerIndex_ + direction + count) % count;
    repeatTimer_ = 0.0f;
    repeatDirection_ = 0;
}

void SettingsScene::AdjustSelectedValue(int direction) {
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

void SettingsScene::UpdateFooterSprites() {
    const uint32_t normalHandle = Sprite::LoadTexture("ui/settings/settings_footer_button.png");
    const uint32_t selectedHandle = Sprite::LoadTexture("ui/settings/settings_footer_button_selected.png");
    const Vector4 normalText = { 0.11f, 0.18f, 0.22f, 0.98f };
    const Vector4 selectedText = { 1.0f, 0.98f, 0.78f, 1.0f };

    for (int i = 0; i < static_cast<int>(FooterAction::Count); ++i) {
        const bool selected = focusArea_ == FocusArea::Footer && i == footerIndex_;
        Sprite* button = footerButtons_[static_cast<size_t>(i)];
        if (button) {
            button->SetTextureHandle(selected ? selectedHandle : normalHandle);
            const auto& metadata = TextureManager::GetInstance()->GetMetadata(selected ? selectedHandle : normalHandle);
            button->SetTextureRect({ 0.0f, 0.0f }, { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
            button->SetColor({ 1.0f, 1.0f, 1.0f, selected ? 1.0f : 0.88f });
        }
        Sprite* text = footerTexts_[static_cast<size_t>(i)];
        if (text) {
            text->SetColor(selected ? selectedText : normalText);
        }
    }
}

void SettingsScene::ExecuteFooterAction() {
    switch (static_cast<FooterAction>(footerIndex_)) {
    case FooterAction::Back:
    case FooterAction::Apply:
        ReturnToTitle();
        break;
    case FooterAction::Reset:
        ResetSettingsToDefault();
        focusArea_ = FocusArea::Items;
        selectedIndex_ = 0;
        break;
    default:
        break;
    }
}

void SettingsScene::ResetSettingsToDefault() {
    GameSettingsManager* settings = GameSettingsManager::GetInstance();
    settings->SetBGMVolume(0.8f);
    settings->SetSEVolume(0.8f);
    settings->SetCameraSensitivity(1.0f);
    settings->Save();
}

SettingsScene::Category SettingsScene::GetItemCategory(Item item) const {
    switch (item) {
    case Item::BGM:
    case Item::SE:
        return Category::Sound;
    case Item::CameraSensitivity:
    default:
        return Category::Control;
    }
}

bool SettingsScene::IsItemVisible(Item item) const {
    return GetItemCategory(item) == GetItemCategory(static_cast<Item>(selectedIndex_));
}

bool SettingsScene::IsSliderItem(Item item) const {
    return item == Item::BGM ||
        item == Item::SE ||
        item == Item::CameraSensitivity;
}

bool SettingsScene::IsChoiceItem(Item item) const {
    (void)item;
    return false;
}

void SettingsScene::ReturnToTitle() {
    GameSettingsManager::GetInstance()->Save();
    DebugConsole::GetInstance()->AddLog("[Settings] Save settings and return to title.");
    SceneManager::GetInstance()->ChangeScene("TITLE");
}

float SettingsScene::GetValue(Item item) const {
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

float SettingsScene::GetNormalizedValue(Item item) const {
    if (item == Item::CameraSensitivity) {
        return std::clamp(GetValue(item), 0.0f, 1.0f);
    }
    return std::clamp(GetValue(item), 0.0f, 1.0f);
}

int SettingsScene::GetDisplayValue(Item item) const {
    return std::clamp(static_cast<int>(std::round(GetNormalizedValue(item) * 100.0f)), 0, 100);
}

std::string SettingsScene::GetDisplayTextTexture(Item item) const {
    (void)item;
    return "";
}

void SettingsScene::SetNumberSprites(OptionRow& row, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color) {
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
        const bool visible = i >= firstVisible;
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

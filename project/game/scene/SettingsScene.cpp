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

    constexpr std::array<const char*, 3> rowPrefixes = {
        "settings_bgm",
        "settings_se",
        "settings_camera"
    };

    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        const std::string prefix = rowPrefixes[static_cast<size_t>(i)];
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
            row.digits[static_cast<size_t>(digit)] =
                FindSprite(prefix + "_value" + std::to_string(digit));
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
        inputManager_->IsKeyTriggered(DIK_RETURN) ||
        inputManager_->IsKeyTriggered(DIK_SPACE) ||
        inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
        ReturnToTitle();
    }
}

void SettingsScene::UpdateSprites(float deltaTime) {
    (void)deltaTime;

    const Vector4 normalText = { 0.78f, 0.92f, 0.96f, 0.78f };
    const Vector4 selectedText = { 1.0f, 0.98f, 0.70f, 1.0f };
    const Vector4 fillNormal = { 0.22f, 0.78f, 0.95f, 0.88f };
    const Vector4 fillSelected = { 1.0f, 0.78f, 0.18f, 0.98f };

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
            const uint32_t backdropHandle = Sprite::LoadTexture(selected ? "ui/settings/settings_row_selected.png" : "ui/settings/settings_row.png");
            row.backdrop->SetTextureHandle(backdropHandle);
            const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(backdropHandle);
            row.backdrop->SetTextureRect({ 0.0f, 0.0f }, { static_cast<float>(metadata.width), static_cast<float>(metadata.height) });
            row.backdrop->SetColor(selected ? Vector4{ 1.0f, 1.0f, 1.0f, 0.96f } : Vector4{ 1.0f, 1.0f, 1.0f, 0.82f });
        }
        if (row.label) {
            row.label->SetColor(selected ? selectedText : normalText);
        }
        if (row.fill) {
            row.fill->SetPosition({ sliderLeft, trackPosition.y });
            row.fill->SetSize({ fillWidth, trackSize.y });
            row.fill->SetColor(LerpColor(fillSelected, { 1.0f, 0.92f, 0.34f, 1.0f }, pulse * 0.45f));
            if (!selected) {
                row.fill->SetColor(fillNormal);
            }
        }
        if (row.knob) {
            const Vector2 knobBaseSize = row.track ? Vector2{ trackSize.y * 1.7f, trackSize.y * 1.7f } : row.knob->GetSize();
            const float knobScale = selected ? kSelectedKnobScale : 1.0f;
            row.knob->SetPosition({ sliderLeft + trackSize.x * normalized, trackPosition.y });
            row.knob->SetSize({ knobBaseSize.x * knobScale, knobBaseSize.y * knobScale });
            row.knob->SetColor(selected ? Vector4{ 1.0f, 0.96f, 0.58f, 1.0f } : Vector4{ 0.84f, 0.96f, 1.0f, 0.9f });
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
        SetNumberSprites(
            row,
            GetDisplayValue(static_cast<Item>(i)),
            valueRightPosition,
            digitHeight,
            selected ? selectedText : normalText
        );
    }
}

void SettingsScene::ChangeSelection(int direction) {
    const int count = static_cast<int>(Item::Count);
    selectedIndex_ = (selectedIndex_ + direction + count) % count;
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
        return std::clamp((GetValue(item) - 0.5f) / 1.5f, 0.0f, 1.0f);
    }
    return std::clamp(GetValue(item), 0.0f, 1.0f);
}

int SettingsScene::GetDisplayValue(Item item) const {
    if (item == Item::CameraSensitivity) {
        return static_cast<int>(std::round(GetValue(item) * 100.0f));
    }
    return static_cast<int>(std::round(GetValue(item) * 100.0f));
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

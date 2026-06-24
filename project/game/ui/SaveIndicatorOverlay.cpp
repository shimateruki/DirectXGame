#define NOMINMAX
#include "SaveIndicatorOverlay.h"

#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteLayoutScaler.h"
#include "TextureManager.h"
#include "json.hpp"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <fstream>

using json = nlohmann::json;

namespace {
constexpr float kFadeInDuration = 0.16f;
constexpr float kFadeOutDuration = 0.30f;
constexpr float kPi = 3.1415926535f;

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

float EaseOutBack(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(value - 1.0f, 3.0f) + c1 * std::pow(value - 1.0f, 2.0f);
}
}

void SaveIndicatorOverlay::Initialize(SpriteCommon* spriteCommon, const std::string& layoutPath) {
    spriteCommon_ = spriteCommon;
    LoadLayout(layoutPath);
    BindSprites();
    SetAllVisible(false);
}

void SaveIndicatorOverlay::Finalize() {
    sprites_.clear();
    panel_ = {};
    icon_ = {};
    text_ = {};
    spriteCommon_ = nullptr;
    isActive_ = false;
    debugHold_ = false;
    timer_ = 0.0f;
}

void SaveIndicatorOverlay::Play(float holdDuration) {
    holdDuration_ = std::max(0.35f, holdDuration);
    timer_ = 0.0f;
    isActive_ = true;
    SetAllVisible(true);
    ApplyVisibilityAnimation(0.0f);
}

void SaveIndicatorOverlay::Update(float deltaTime) {
    if (!IsActive()) {
        return;
    }

    timer_ += deltaTime;

    float alpha = 1.0f;
    if (!debugHold_) {
        const float totalDuration = kFadeInDuration + holdDuration_ + kFadeOutDuration;
        if (timer_ < kFadeInDuration) {
            alpha = timer_ / kFadeInDuration;
        } else if (timer_ > kFadeInDuration + holdDuration_) {
            alpha = 1.0f - (timer_ - kFadeInDuration - holdDuration_) / kFadeOutDuration;
        }

        if (timer_ >= totalDuration) {
            isActive_ = false;
            SetAllVisible(false);
            return;
        }
    }

    ApplyVisibilityAnimation(std::clamp(alpha, 0.0f, 1.0f));

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Update();
        }
    }
}

void SaveIndicatorOverlay::Draw() {
    if (!IsActive()) {
        return;
    }

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Draw();
        }
    }
}

#ifdef USE_IMGUI
void SaveIndicatorOverlay::DrawImGui() {
    if (ImGui::Button("セーブ中表示を再生", ImVec2(-1.0f, 28.0f))) {
        Play(1.35f);
    }

    bool hold = debugHold_;
    if (ImGui::Checkbox("セーブ中表示を固定", &hold)) {
        debugHold_ = hold;
        if (debugHold_) {
            timer_ = 0.0f;
            SetAllVisible(true);
        } else if (!isActive_) {
            SetAllVisible(false);
        }
    }
}
#endif

void SaveIndicatorOverlay::LoadLayout(const std::string& layoutPath) {
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
        const std::string name = spriteData.value("name", "save_indicator_sprite");
        const std::string texture = spriteData.value("texture", "common/white.png");
        const uint32_t textureHandle = Sprite::LoadTexture(texture.empty() ? "common/white.png" : texture);

        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon_, textureHandle);
        sprite->SetName(name);
        sprite->SetTextureName(texture);
        sprite->SetPosition(SpriteLayoutScaler::ScalePosition(
            SpriteLayoutScaler::ReadVector2(spriteData.value("position", json::array()), sprite->GetPosition()),
            layoutScale
        ));
        sprite->SetSize(SpriteLayoutScaler::ScaleSize(
            SpriteLayoutScaler::ReadVector2(spriteData.value("size", json::array()), sprite->GetSize()),
            layoutScale
        ));
        sprite->SetAnchorPoint(SpriteLayoutScaler::ReadVector2(spriteData.value("anchor", json::array()), sprite->GetAnchorPoint()));
        sprite->SetColor(ReadColor(spriteData.value("color", json::array()), sprite->GetColor()));
        sprite->SetEmissive(spriteData.value("emissive", 1.0f));
        sprite->SetVisible(false);
        sprite->Update();
        sprites_.push_back(std::move(sprite));
    }
}

void SaveIndicatorOverlay::BindSprites() {
    panel_.sprite = FindSprite("save_indicator_panel");
    icon_.sprite = FindSprite("save_indicator_icon");
    text_.sprite = FindSprite("save_indicator_text");

    CaptureBaseState(panel_);
    CaptureBaseState(icon_);
    CaptureBaseState(text_);
}

Sprite* SaveIndicatorOverlay::FindSprite(const std::string& name) const {
    for (const auto& sprite : sprites_) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr;
}

void SaveIndicatorOverlay::SetAllVisible(bool visible) {
    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->SetVisible(visible);
        }
    }
}

void SaveIndicatorOverlay::CaptureBaseState(SpriteState& state) {
    if (!state.sprite) {
        return;
    }

    state.basePosition = state.sprite->GetPosition();
    state.baseSize = state.sprite->GetSize();
    state.baseColor = state.sprite->GetColor();
    state.baseRotation = state.sprite->GetRotation();
}

void SaveIndicatorOverlay::ApplyVisibilityAnimation(float alpha) {
    const float appear = EaseOutBack(std::min(timer_ / kFadeInDuration, 1.0f));
    const float pulse = std::sin(timer_ * 5.0f);

    if (panel_.sprite) {
        Vector4 color = panel_.baseColor;
        color.w *= alpha;
        panel_.sprite->SetColor(color);
        panel_.sprite->SetSize({
            panel_.baseSize.x * (0.92f + 0.08f * appear),
            panel_.baseSize.y * (0.92f + 0.08f * appear)
        });
        panel_.sprite->SetPosition({
            panel_.basePosition.x,
            panel_.basePosition.y + (1.0f - appear) * 10.0f
        });
    }

    if (icon_.sprite) {
        const float iconScale = (0.82f + 0.18f * appear) * (1.0f + 0.045f * pulse);
        Vector4 color = icon_.baseColor;
        color.w *= alpha;
        icon_.sprite->SetColor(color);
        icon_.sprite->SetSize({
            icon_.baseSize.x * iconScale,
            icon_.baseSize.y * iconScale
        });
        icon_.sprite->SetPosition({
            icon_.basePosition.x,
            icon_.basePosition.y + std::sin(timer_ * 6.0f) * 2.0f
        });
        icon_.sprite->SetRotation(icon_.baseRotation + timer_ * kPi * 0.72f);
    }

    if (text_.sprite) {
        Vector4 color = text_.baseColor;
        color.w *= alpha * (0.88f + 0.12f * std::sin(timer_ * 7.0f));
        text_.sprite->SetColor(color);
        text_.sprite->SetSize({
            text_.baseSize.x * (0.94f + 0.06f * appear),
            text_.baseSize.y * (0.94f + 0.06f * appear)
        });
        text_.sprite->SetPosition({
            text_.basePosition.x + std::sin(timer_ * 3.3f) * 1.5f,
            text_.basePosition.y
        });
    }
}

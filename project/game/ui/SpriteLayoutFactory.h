#pragma once

#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteLayoutScaler.h"

#include <memory>

namespace SpriteLayoutFactory {

// JSON の基本設定だけを適用します。切り抜きなど画面固有の設定後に Update してください。
inline std::unique_ptr<engine::Sprite> Create(engine::SpriteCommon* spriteCommon, const nlohmann::json& spriteData,
    const SpriteLayoutScaler::LayoutScale& layoutScale, const char* fallbackName) {
    const std::string name = SpriteLayoutScaler::ReadString(spriteData, "name", fallbackName);
    const std::string texture = SpriteLayoutScaler::ReadString(spriteData, "texture", "common/white.png");
    const uint32_t textureHandle = engine::Sprite::LoadTexture(texture.empty() ? "common/white.png" : texture);

    auto sprite = std::make_unique<engine::Sprite>();
    sprite->Initialize(spriteCommon, textureHandle);
    sprite->SetName(name);
    sprite->SetTextureName(texture);
    sprite->SetPosition(SpriteLayoutScaler::ScalePosition(
        SpriteLayoutScaler::ReadVector2(spriteData.value("position", nlohmann::json::array()), sprite->GetPosition()),
        layoutScale));
    sprite->SetSize(SpriteLayoutScaler::ScaleSize(
        SpriteLayoutScaler::ReadVector2(spriteData.value("size", nlohmann::json::array()), sprite->GetSize()), layoutScale));
    sprite->SetAnchorPoint(
        SpriteLayoutScaler::ReadVector2(spriteData.value("anchor", nlohmann::json::array()), sprite->GetAnchorPoint()));
    sprite->SetColor(SpriteLayoutScaler::ReadColor(spriteData.value("color", nlohmann::json::array()), sprite->GetColor()));
    sprite->SetEmissive(SpriteLayoutScaler::ReadFloat(spriteData, "emissive", 1.0f));
    sprite->SetVisible(false);
    return sprite;
}

} // namespace SpriteLayoutFactory

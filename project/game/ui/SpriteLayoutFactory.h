#pragma once

// 各画面が配置JSONからSpriteを生成し、名前と基本姿勢を設定する共通入口。表示条件や入力処理は画面側が担当する。

#include "Sprite.h"
#include "SpriteCommon.h"
#include "game/ui/SpriteLayoutScaler.h"

#include <memory>

namespace SpriteLayoutFactory {

// JSON の基本設定だけを適用します。切り抜きなど画面固有の設定後に Update してください。
// 生成したSpriteの所有権を返す。初期状態は非表示で、表示とUpdateは呼び出し側で確定する。
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

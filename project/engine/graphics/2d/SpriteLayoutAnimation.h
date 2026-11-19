#pragma once

#include "Sprite.h"
#include "TextureManager.h"
#include "game/ui/SpriteLayoutScaler.h"

// スプライトレイアウトのコマ設定を、Editorプレビューとゲームで同じ時刻から再生します。
namespace SpriteLayoutAnimation {

struct Settings {
    int frameCount = 1; // 横一列に並んだコマ数。
    float fps = 12.0f;
    bool loop = true;
};

inline Settings Read(const nlohmann::json& row) {
    const auto animation = row.find("animation");
    if (animation == row.end() || !animation->is_object())
        return {};
    return {
        std::clamp(SpriteLayoutScaler::ReadInt(*animation, "frameCount", 1), 1, 128),
        std::clamp(SpriteLayoutScaler::ReadFloat(*animation, "fps", 12.0f), 1.0f, 60.0f),
        SpriteLayoutScaler::ReadBool(*animation, "loop", true)
    };
}

// 経過秒は再生開始を0とし、周回または最終コマへ収めてUVだけを設定します。
inline void Apply(engine::Sprite& sprite, const Settings& settings, double seconds) {
    const auto handle = sprite.GetTextureHandle();
    if (settings.frameCount <= 1 || handle == 0)
        return;
    const auto& texture = TextureManager::GetInstance()->GetMetadata(handle);
    if (texture.width < static_cast<size_t>(settings.frameCount) || texture.height == 0 ||
        texture.width % static_cast<size_t>(settings.frameCount) != 0)
        return;

    const double index = std::floor((std::max)(0.0, seconds) * settings.fps);
    const double frame = settings.loop ? std::fmod(index, settings.frameCount)
                                       : (std::min)(index, static_cast<double>(settings.frameCount - 1));
    const float width = static_cast<float>(texture.width / static_cast<size_t>(settings.frameCount));
    sprite.SetTextureRect({static_cast<float>(frame) * width, 0.0f}, {width, static_cast<float>(texture.height)});
}

} // namespace SpriteLayoutAnimation
